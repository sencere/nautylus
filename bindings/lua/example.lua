local nautylus = require("nautylus")

local db = "lua-binding.ng"
os.remove(db)

local graph = nautylus.create(db)
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

io.write(graph:query("MATCH (a:Person)-[r:KNOWS]->(b:Person) RETURN a.name, r.since, b.name"))
io.write(graph:query('CREATE (ada:Person {name: "Ada"}) RETURN ada.name', true))

graph:save()
graph:close()

local opened = nautylus.open(db)
print("nodes=" .. opened:node_count() .. " relationships=" .. opened:relationship_count())
opened:close()
