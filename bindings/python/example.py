from pathlib import Path

from nautylus import Graph


db = Path("python-binding.ng")
if db.exists():
    db.unlink()

with Graph.create(db) as graph:
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

    print(graph.query("MATCH (a:Person)-[r:KNOWS]->(b:Person) RETURN a.name, r.since, b.name"))

    print(
        graph.query(
            'CREATE (ada:Person {name: "Ada"}) RETURN ada.name',
            mutate=True,
        )
    )
    graph.save()

with Graph.open(db) as graph:
    print(f"nodes={graph.node_count()} relationships={graph.relationship_count()}")
