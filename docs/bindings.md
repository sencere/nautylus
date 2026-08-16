# Language Bindings

Nautylus ships lightweight FFI bindings for Python, PHP, and LuaJIT. They use
the shared C library built by `make bindings`:

```sh
make bindings
```

The default shared-library path is `build/libnautylus.so`. Set `NAUTYLUS_LIB`
when loading a library from another location.

## Python

The Python binding uses the standard-library `ctypes` module and has no package
dependency.

Run the example:

```sh
make bindings
PYTHONPATH=bindings/python python3 bindings/python/example.py
```

Minimal usage:

```python
from nautylus import Graph

with Graph.create("python.ng") as graph:
    person = graph.symbol("Person")
    knows = graph.symbol("KNOWS")
    name = graph.symbol("name")
    since = graph.symbol("since")

    joe = graph.create_node([person])
    bob = graph.create_node([person])
    rel = graph.create_relationship(joe, knows, bob)

    graph.set_node(joe, name, "Joe")
    graph.set_node(bob, name, "Bob")
    graph.set_relationship(rel, since, 2020)

    print(graph.query(
        "MATCH (a:Person)-[r:KNOWS]->(b:Person) "
        "RETURN a.name, r.since, b.name"
    ))
    graph.save()
```

## PHP

The PHP binding uses PHP FFI. The PHP runtime must have FFI enabled.

Run the example:

```sh
make bindings
php bindings/php/example.php
```

Minimal usage:

```php
require 'bindings/php/Nautylus.php';

$graph = NautylusGraph::create('php.ng');
$person = $graph->symbol('Person');
$knows = $graph->symbol('KNOWS');
$name = $graph->symbol('name');
$since = $graph->symbol('since');

$joe = $graph->createNode([$person]);
$bob = $graph->createNode([$person]);
$rel = $graph->createRelationship($joe, $knows, $bob);

$graph->setNode($joe, $name, 'Joe');
$graph->setNode($bob, $name, 'Bob');
$graph->setRelationship($rel, $since, 2020);

echo $graph->query(
    'MATCH (a:Person)-[r:KNOWS]->(b:Person) RETURN a.name, r.since, b.name'
);
$graph->save();
$graph->close();
```

## Lua

The Lua binding uses LuaJIT FFI. Run it with `luajit`; plain Lua does not expose
the required FFI module.

Run the example:

```sh
make bindings
LUA_PATH='bindings/lua/?.lua;;' luajit bindings/lua/example.lua
```

From inside `bindings/lua`, this also works:

```sh
luajit example.lua
```

Minimal usage:

```lua
local nautylus = require("nautylus")

local graph = nautylus.create("lua.ng")
local person = graph:symbol("Person")
local knows = graph:symbol("KNOWS")
local name = graph:symbol("name")
local since = graph:symbol("since")

local joe = graph:create_node({ person })
local bob = graph:create_node({ person })
local rel = graph:create_relationship(joe, knows, bob)

graph:set_node(joe, name, "Joe")
graph:set_node(bob, name, "Bob")
graph:set_relationship(rel, since, 2020)

io.write(graph:query(
    "MATCH (a:Person)-[r:KNOWS]->(b:Person) RETURN a.name, r.since, b.name"
))
graph:save()
graph:close()
```

## Current Binding Surface

The Python, PHP, and Lua bindings currently expose:

* `create` / `open` / `close` / `save`;
* symbol interning;
* node creation with labels;
* relationship creation;
* scalar node and relationship properties: string, int64, double, bool;
* node and relationship counts;
* query execution to string output, including mutating queries.

The bindings intentionally do not expose every C API yet. For analytics,
GraphSAGE, vector search, custom procedures, and callback-heavy APIs, call the C
API directly for now.
