"""Test files: NAME.ref in a suite's directory (tests/reg/g1) or in a directory below it, one
test each. A test's name is its file's path below the suite's directory without the ".ref": evenai/wake.

A file is a sequence of Python expressions, one assert or statement each (comments allowed), evaluated with
only the assert and statement constructors (asserts.py, statements.py, and those of the suite's protocol, the
suite.py of its directory), the input vocabulary and the lens names in scope: no imports, Python statements,
operators or attribute access. include("common/fault") inlines common/fault.ref of the suite's directory. The
named frames are its frames.txt ("name sha12" lines), the packet buffers its packets/NAME.hex (buf()).

A mistake in a file is a LoadError with the file and the line, whatever goes wrong while the line is
evaluated. What can be told without running is checked when the file is loaded: every label an assert names
is defined by a line (before it, for an assert inside a line), every panel content it names is in frames.txt.
"""
import ast
import importlib.util
import os
import re

from . import asserts, protocol, statements
from .base import RefError

SHARED = ("common", "packets", "frames")    # directories of a suite that hold no tests


class LoadError(Exception):
    pass


class Include:
    def __init__(self, name):
        self.name = name


class Suite:
    """The tests of a directory, and the protocol their phone speaks: PROTOCOL of the directory's suite.py."""

    def __init__(self, directory):
        self.dir = os.path.abspath(directory)
        self.name = os.path.basename(self.dir)
        path = os.path.join(self.dir, "suite.py")
        if not os.path.isfile(path):
            raise LoadError("no %s" % path)
        spec = importlib.util.spec_from_file_location("suite_" + self.name, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.proto = getattr(module, "PROTOCOL", None)
        if not isinstance(self.proto, protocol.Protocol):
            raise LoadError("%s: no PROTOCOL = an instance of engine.protocol.Protocol" % path)
        self.buffers = statements.Buffers(os.path.join(self.dir, "packets"))
        own = {**asserts.NAMES, **statements.NAMES, "buf": self.buffers.buf}
        clash = (set(own) & set(self.proto.NAMES)) ^ set(self.proto.REPLACES)
        if clash:
            raise LoadError("%s: REPLACES must list exactly the names of the engine that NAMES gives another "
                            "meaning; it differs on: %s" % (path, ", ".join(sorted(clash))))
        self.names = {**own, **self.proto.NAMES}
        self.found = self.known = None

    def files(self):
        """{name: path} of every test of the suite: the .ref files of its directory and of those below it."""
        if self.found is None:
            found = {}
            for root, dirs, names in os.walk(self.dir):
                dirs[:] = sorted(d for d in dirs if d[:1] not in "._" and not (root == self.dir and d in SHARED))
                for n in sorted(n for n in names if n.endswith(".ref")):
                    path = os.path.join(root, n)
                    found[os.path.relpath(path, self.dir).replace(os.sep, "/")[:-4]] = path
            self.found = found
        return self.found

    def tests(self):
        """The names of the suite's tests, in order."""
        return sorted(self.files())

    def relative(self, path):
        """A file of the suite as it is named in a report: g1/evenai/wake.ref."""
        return os.path.relpath(path, os.path.dirname(self.dir)).replace(os.sep, "/")

    def frames(self):
        """{name: sha12} of frames.txt: the panel contents a test can name."""
        if self.known is None:
            path, out = os.path.join(self.dir, "frames.txt"), {}
            if os.path.isfile(path):
                with open(path, encoding="utf-8") as f:
                    for n, line in enumerate(f, 1):
                        parts = line.split("#")[0].split()
                        if parts and (len(parts) != 2 or not re.fullmatch(r"[0-9a-f]{12}", parts[1])):
                            raise LoadError("%s:%d: want 'name sha12', got %r" % (self.relative(path), n, line.strip()))
                        if parts and (parts[0] == "blank" or parts[0] in out):
                            raise LoadError("%s:%d: %s" % (self.relative(path), n, "the name %s is %s" % (
                                parts[0], "built in (a dark panel)" if parts[0] == "blank" else "there twice")))
                        if parts:
                            out[parts[0]] = parts[1]
            self.known = out
        return self.known

    def load(self, name):
        """The asserts and statements of the test, those of its includes in their place."""
        if name not in self.files():
            raise LoadError("no test %s in %s" % (name, self.name))
        loaded = self.read(self.files()[name], ())
        self.check(loaded)
        return loaded

    def read(self, path, seen):
        where = self.relative(path)
        if path in seen:
            raise LoadError("%s includes itself" % where)
        if not os.path.isfile(path):
            raise LoadError("no file %s" % where)
        with open(path, encoding="utf-8") as f:
            source = f.read()
        try:
            tree = ast.parse(source, path)
        except SyntaxError as e:
            raise LoadError("%s:%s: %s" % (where, e.lineno, e.msg))
        out = []
        for stmt in tree.body:
            if not isinstance(stmt, ast.Expr):
                raise LoadError("%s:%d: only assert and statement expressions are allowed" % (where, stmt.lineno))
            value = self.value(stmt.value, where)
            if isinstance(value, Include):
                target = os.path.join(self.dir, value.name + ".ref")
                if not os.path.isfile(target):
                    raise LoadError("%s:%d: include(%r): no file %s" % (
                        where, stmt.lineno, value.name, self.relative(target)))
                inner = self.read(target, seen + (path,))
                for x in inner:
                    if isinstance(x, statements.Statement):
                        x.included = True       # not the file's own line (unused_labels())
                out += inner
            elif isinstance(value, (asserts.Assert, statements.Statement)):
                inherit(value)
                out.append(value)
            else:
                raise LoadError("%s:%d: not an assert or statement: %r" % (where, stmt.lineno, value))
        return out

    def value(self, node, path):
        """The value of an expression of a test file."""
        where = "%s:%d" % (path, getattr(node, "lineno", 0))
        if isinstance(node, ast.Constant) and isinstance(node.value, bytes):
            raise LoadError("%s: a packet is written send('AA BB ..'), not as a bytes literal" % where)
        if isinstance(node, ast.Constant) and isinstance(node.value, (str, int, float, bool, type(None))):
            return node.value
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
            operand = self.value(node.operand, path)
            if isinstance(operand, bool) or not isinstance(operand, (int, float)):
                raise LoadError("%s: - stands before a number, not %r" % (where, operand))
            return -operand
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):       # OPT + "Lux 300"
            left, right = self.value(node.left, path), self.value(node.right, path)
            if isinstance(left, str) and isinstance(right, str):
                return left + right
            raise LoadError("%s: + joins two strings (a monitor command), not %s and %s" % (
                where, type(left).__name__, type(right).__name__))
        if isinstance(node, ast.Name):
            if node.id in self.names:
                return self.names[node.id]
            raise LoadError("%s: unknown name %r" % (where, node.id))
        if isinstance(node, (ast.Tuple, ast.List)):
            return tuple(self.value(e, path) for e in node.elts)
        if isinstance(node, ast.Call):
            return self.call(node, path, where)
        raise LoadError("%s: %s is not allowed in a test file" % (where, type(node).__name__))

    def call(self, node, path, where):
        if not isinstance(node.func, ast.Name):
            raise LoadError("%s: only the constructors of asserts and statements can be called" % where)
        if any(isinstance(a, ast.Starred) for a in node.args) or any(k.arg is None for k in node.keywords):
            raise LoadError("%s: * and ** are not allowed in a test file" % where)
        args = [self.value(a, path) for a in node.args]
        kwargs = {k.arg: self.value(k.value, path) for k in node.keywords}
        if node.func.id == "include":
            if len(args) != 1 or kwargs or not isinstance(args[0], str) or not re.fullmatch(r"[\w/]+", args[0]):
                raise LoadError("%s: include() takes the name of a file of the suite, include(\"common/fault\")"
                                % where)
            return Include(args[0])
        func = self.names.get(node.func.id)
        if not callable(func):
            raise LoadError("%s: unknown constructor %r" % (where, node.func.id))
        try:
            made = func(*args, **kwargs)
        except LoadError:
            raise
        except RefError as e:
            raise LoadError("%s: %s" % (where, e))
        except TypeError as e:                  # wrong arguments for the constructor
            raise LoadError("%s: %s" % (where, e))
        except Exception as e:                  # whatever else a constructor makes of what the file gave it
            raise LoadError("%s: %s(): %s: %s" % (where, node.func.id, type(e).__name__, e))
        if isinstance(made, (asserts.Assert, statements.Statement)) and made.where is None:
            made.where = where
        return made

    def check(self, loaded):
        """What can be told of a loaded file without running it: the labels its asserts name are defined, as
        often as the assert says, and the panel contents they name are in frames.txt."""
        times = {}
        for x in loaded:
            for label in labels(x):
                times[label] = times.get(label, 0) + 1
        so_far = {}
        for x in loaded:
            for label in labels(x):
                so_far[label] = so_far.get(label, 0) + 1
            defined = so_far if isinstance(x, statements.Statement) else times
            for a in [a for top in tops(x) for a in walk(top)]:
                for anchor in a.anchors():
                    self.check_anchor(a, anchor, defined, x)
                for frame in a.frames():
                    try:
                        asserts.sha_of(frame, self.frames())
                    except RefError as e:
                        raise LoadError("%s: %s" % (a.where or x.where, e))
                for text in texts(getattr(a, "pattern", None)):
                    if not self.proto.begins(text):
                        raise LoadError("%s: %s: no item of a trace starts with %r (the phone's lines: SAYS in %s/"
                                        "suite.py, the engine's: KINDS in engine/trace.py)" % (
                                            a.where or x.where, a.text[:60], text, self.name))

    def check_anchor(self, a, anchor, defined, top):
        """defined: {label: times}, in the whole file for an assert that stands alone (top), up to its line
        for one inside a line."""
        label, where, alone = asserts.label_of(anchor), a.where or top.where, isinstance(top, asserts.Assert)
        if label is None:
            return
        if label not in defined:
            raise LoadError("%s: %s names the label %r, which no line %s defines (label=%r on an action, or "
                            "mark(%r))" % (where, a.text[:60], label, "of the file" if alone else "before it",
                                           label, label))
        n = next((p for p in anchor[1:] if isinstance(p, int)), None) if isinstance(anchor, tuple) else None
        if isinstance(anchor, tuple) and any(isinstance(p, str) for p in anchor[1:]):
            return                              # ("label", lens): how often it is on that lens is known in the run
        if n is not None and n > defined[label]:
            raise LoadError("%s: %s names occurrence %d of the label %r, which is defined %d time(s)%s" % (
                where, a.text[:60], n, label, defined[label], "" if alone else " up to this line"))
        if n is None and defined[label] > 1 and not (isinstance(a, asserts.After) and a.every):
            raise LoadError("%s: the label %r is defined %d times%s: %s must say which, (%r, n), or be each()" % (
                where, label, defined[label], "" if alone else " up to this line", a.text[:60], label))

    def unused_labels(self, loaded):
        """The labels the file's own lines define and no assert names: [(where, label)]."""
        named = {asserts.label_of(anchor) for x in loaded for top in tops(x) for a in walk(top)
                 for anchor in a.anchors()}
        return [(x.where, label) for x in loaded if not getattr(x, "included", False)
                for label in labels(x) if label not in named]


def tops(x):
    """The asserts of a line of a file: the assert it is, or those the statement carries."""
    return [x] if isinstance(x, asserts.Assert) else x.inner()


def labels(x):
    """The labels a line of a file defines."""
    return x.labels() if isinstance(x, statements.Statement) else ()


def texts(pattern):
    """The texts an item must start with for the pattern to match."""
    if isinstance(pattern, asserts.Starts):
        return [pattern.text]
    return [t for p in pattern.patterns for t in texts(p)] if isinstance(pattern, asserts.Either) else []


def walk(a):
    """An assert and the asserts it is made of."""
    yield a
    for c in a.inner():
        yield from walk(c)


def inherit(value, where=None):
    """Give the asserts a constructor made inside another (link_up()) the line of the one they are in."""
    if value.where is None:
        value.where = where
    for c in value.inner():
        inherit(c, value.where)
