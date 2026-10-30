"""Integration tests: run the real mini_redis_server binary and talk to it over TCP.

Usage:  python3 tests/integration/test_server.py <path-to-mini_redis_server> [unittest args]
CTest runs this automatically (see CMakeLists.txt).

Standard library only, so the test suite stays dependency-free. Each test starts
its own server on a free port inside a temporary directory, so tests can't see
each other's data or AOF files.
"""
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest

SERVER_BINARY = None  # set from the command line in __main__


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def describe_exit(returncode):
    """Turns -13 into 'killed by SIGPIPE' (POSIX reports signals as negative codes)."""
    if returncode is not None and returncode < 0:
        try:
            return f"killed by {signal.Signals(-returncode).name}"
        except ValueError:
            pass
    return f"exit code {returncode}"


class ServerProcess:
    """A running mini_redis_server on a free port, working in `workdir`."""

    def __init__(self, *args, workdir=None):
        self._tmp = None
        if workdir is None:
            self._tmp = tempfile.TemporaryDirectory()
            workdir = self._tmp.name
        self.workdir = workdir
        self.port = free_port()
        # Server output goes to a file, not a pipe: an unread pipe fills up and blocks the server.
        self._log = open(os.path.join(workdir, "server.log"), "w+")
        self.proc = subprocess.Popen(
            [SERVER_BINARY, "--port", str(self.port), *args],
            cwd=workdir, stdout=self._log, stderr=subprocess.STDOUT)
        self._wait_until_listening()

    def _wait_until_listening(self, timeout=5.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError(f"server exited during startup ({describe_exit(self.proc.returncode)}):\n{self.output()}")
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.2).close()
                return
            except OSError:
                time.sleep(0.05)
        self.stop()
        raise RuntimeError(f"server did not listen on port {self.port} within {timeout} s")

    def is_running(self):
        return self.proc.poll() is None

    def output(self):
        self._log.flush()
        self._log.seek(0)
        return self._log.read()

    def stop(self):
        if self.is_running():
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self._log.close()
        if self._tmp is not None:
            self._tmp.cleanup()


def exchange_raw(port, data, timeout=2.0):
    """Sends raw bytes and returns everything the server sends back until it closes
    the connection. Raises TimeoutError if it neither replies nor closes."""
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as s:
        s.sendall(data)
        received = b""
        while True:
            chunk = s.recv(4096)
            if not chunk:
                return received
            received += chunk


class RespError(Exception):
    """An error reply (-ERR ...) from the server."""


class Client:
    """Minimal RESP client: sends commands as RESP arrays, parses one reply per call."""

    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.reader = self.sock.makefile("rb")

    def call(self, *args):
        self.sock.sendall(self.encode(args))
        return self.read_reply()

    @staticmethod
    def encode(args):
        parts = [b"*%d\r\n" % len(args)]
        for arg in args:
            data = arg if isinstance(arg, bytes) else str(arg).encode()
            parts.append(b"$%d\r\n%s\r\n" % (len(data), data))
        return b"".join(parts)

    def read_reply(self):
        line = self.reader.readline()
        if not line.endswith(b"\r\n"):
            raise ConnectionError(f"connection closed mid-reply: {line!r}")
        kind, payload = line[:1], line[1:-2]
        if kind == b"+":
            return payload
        if kind == b"-":
            raise RespError(payload.decode())
        if kind == b":":
            return int(payload)
        if kind == b"$":
            length = int(payload)
            return None if length < 0 else self.reader.read(length + 2)[:-2]
        if kind == b"*":
            return [self.read_reply() for _ in range(int(payload))]
        raise ValueError(f"unexpected reply: {line!r}")

    def close(self):
        self.reader.close()
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


class ServerTestCase(unittest.TestCase):
    def start_server(self, *args, workdir=None):
        server = ServerProcess(*args, workdir=workdir)
        self.addCleanup(server.stop)
        return server


class BasicCommandTest(ServerTestCase):
    def setUp(self):
        self.server = self.start_server("--appendonly", "no")

    def test_ping(self):
        with Client(self.server.port) as c:
            self.assertEqual(c.call("PING"), b"PONG")

    def test_set_get_binary_safe_value(self):
        value = b"hello world\r\nwith a newline"
        with Client(self.server.port) as c:
            self.assertEqual(c.call("SET", "greeting", value), b"OK")
            self.assertEqual(c.call("GET", "greeting"), value)
            self.assertIsNone(c.call("GET", "missing"))

    def test_bgrewriteaof_reports_error_when_aof_is_disabled(self):
        with Client(self.server.port) as c:
            with self.assertRaises(RespError):
                c.call("BGREWRITEAOF")


class ClientDisconnectTest(ServerTestCase):
    def test_client_closing_before_reading_replies_does_not_kill_server(self):
        # Regression test for SIGPIPE. On Linux, send() to a client that has
        # already closed its socket raises SIGPIPE, whose default action
        # terminates the process: one client could take the server down.
        server = self.start_server("--appendonly", "no")
        for _ in range(5):
            s = socket.create_connection(("127.0.0.1", server.port))
            s.sendall(b"PING\r\n" * 10000)
            s.close()  # close without reading a single reply
        time.sleep(0.5)  # give the server time to answer into the closed sockets

        self.assertTrue(server.is_running(),
                        f"server died ({describe_exit(server.proc.returncode)})\n{server.output()}")
        with Client(server.port) as c:
            self.assertEqual(c.call("PING"), b"PONG")


class ProtocolErrorTest(ServerTestCase):
    """Malformed input gets "-ERR Protocol error: ..." and the connection is closed,
    as in Redis. It used to be treated as an incomplete command, so the client
    waited forever while its buffer could grow without limit."""

    def setUp(self):
        self.server = self.start_server("--appendonly", "no")

    def assertProtocolError(self, data, reason):
        self.assertEqual(exchange_raw(self.server.port, data),
                         b"-ERR Protocol error: " + reason + b"\r\n")

    def test_non_numeric_array_length(self):
        self.assertProtocolError(b"*abc\r\n", b"invalid multibulk length")

    def test_negative_bulk_length(self):
        self.assertProtocolError(b"*1\r\n$-5\r\n", b"invalid bulk length")

    def test_bulk_length_over_512_mb(self):
        # Used to make the server wait for 2 GB that would never come.
        self.assertProtocolError(b"*1\r\n$2000000000\r\n", b"invalid bulk length")

    def test_trailing_garbage_after_length(self):
        # std::stoi read "3abc" as 3, so this ran a command named "foo".
        self.assertProtocolError(b"*1\r\n$3abc\r\nfoo\r\n", b"invalid bulk length")

    def test_inline_line_over_64_kb(self):
        # Exactly the smallest size that is an error, so the server has read every
        # byte when it closes. Closing with unread data makes the kernel send RST,
        # which can discard the error reply before the client reads it.
        self.assertProtocolError(b"a" * (64 * 1024 + 2), b"too big inline request")

    def test_other_clients_unaffected(self):
        with Client(self.server.port) as c:
            exchange_raw(self.server.port, b"*abc\r\n")
            self.assertEqual(c.call("PING"), b"PONG")


class CommandLineTest(ServerTestCase):
    def run_server(self, *args):
        with tempfile.TemporaryDirectory() as workdir:
            return subprocess.run([SERVER_BINARY, *args], cwd=workdir, capture_output=True, timeout=5)

    def test_help_exits_zero(self):
        result = self.run_server("--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn(b"--maxkeys", result.stdout)

    def test_invalid_arguments_exit_with_usage_error(self):
        cases = [
            ("--port", "0"), ("--port", "65536"), ("--port", "80abc"), ("--port", "-1"),
            ("--port",), ("--maxkeys", "0"), ("--maxkeys", "ten"),
            ("--appendonly", "maybe"), ("--appendfilename", ""), ("--bogus",),
        ]
        for args in cases:
            with self.subTest(args=args):
                result = self.run_server(*args)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertTrue(result.stderr, "expected an error message on stderr")

    def test_unopenable_aof_file_fails_fast(self):
        # Running without persistence while the user believes it is on would be worse than not starting.
        result = self.run_server("--appendfilename", os.path.join("no", "such", "dir", "x.aof"))
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"Cannot open AOF file", result.stderr)

    def test_maxkeys_limits_key_count(self):
        server = self.start_server("--appendonly", "no", "--maxkeys", "2")
        with Client(server.port) as c:
            for key in ("a", "b", "c"):
                c.call("SET", key, "1")
            self.assertIsNone(c.call("GET", "a"))  # least recently used, so evicted
            self.assertEqual(c.call("GET", "c"), b"1")

    def test_appendonly_no_writes_no_file(self):
        server = self.start_server("--appendonly", "no")
        with Client(server.port) as c:
            c.call("SET", "k", "v")
        self.assertFalse(os.path.exists(os.path.join(server.workdir, "appendonly.aof")))

    def test_appendfilename_is_written_and_replayed_after_restart(self):
        with tempfile.TemporaryDirectory() as workdir:
            first = self.start_server("--appendfilename", "custom.aof", workdir=workdir)
            with Client(first.port) as c:
                c.call("SET", "k", "v")
                c.call("RPUSH", "list", "x", "y")
            first.stop()
            self.assertTrue(os.path.exists(os.path.join(workdir, "custom.aof")))

            second = self.start_server("--appendfilename", "custom.aof", workdir=workdir)
            with Client(second.port) as c:
                self.assertEqual(c.call("GET", "k"), b"v")
                self.assertEqual(c.call("LRANGE", "list", 0, -1), [b"x", b"y"])
            second.stop()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(f"usage: {sys.argv[0]} <path-to-mini_redis_server> [unittest args]")
    SERVER_BINARY = os.path.abspath(sys.argv.pop(1))
    unittest.main()
