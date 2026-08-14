-- Each statement is intended to be run separately.
-- The web interface currently executes one query at a time.

-- Create independent nodes.
CREATE (joe:Person {name: "Joe", age: 34, city: "Berlin"}),
       (bob:Person {name: "Bob", age: 31, city: "Berlin"}),
       (acme:Company {name: "Acme"})
RETURN joe, bob, acme

-- Create connected patterns.
CREATE (joe:Person {name: "Joe"})-[:KNOWS {since: 2020}]->(bob:Person {name: "Bob"})
RETURN joe, bob

-- Create reverse relationships.
CREATE (joe:Person {name: "Joe"})<-[:KNOWS]-(bob:Person {name: "Bob"})
RETURN joe, bob

-- Query all nodes.
MATCH (n)
RETURN n
LIMIT 20

-- Match by label and property.
MATCH (p:Person)
WHERE p.name = "Joe"
RETURN p.name, p.age, id(p)

-- Match relationships.
MATCH (a:Person)-[r:KNOWS]->(b:Person)
RETURN a.name, r.since, b.name
ORDER BY a.name ASC, b.name ASC

-- OPTIONAL MATCH preserves the left row when no right-side match exists.
MATCH (a:Person)
OPTIONAL MATCH (a)-[:WORKS_AT]->(c:Company)
RETURN a.name, c.name
ORDER BY a.name

-- WITH projection and scope.
MATCH (a:Person)
WITH a.city AS city, a.age + 1 AS age_next_year
RETURN city, age_next_year
ORDER BY age_next_year DESC

-- WITH filtering.
MATCH (a:Person)
WITH a
WHERE a.age >= 18
RETURN a.name

-- Aggregates and grouping.
MATCH (a:Person)
RETURN a.city, count(a) AS people, collect(a.name) AS names
ORDER BY people DESC, a.city ASC

-- Aggregate filtering after WITH.
MATCH (a:Person)
WITH a.city AS city, count(a) AS people
WHERE people > 1
RETURN city, people

-- DISTINCT.
MATCH (a:Person)
WITH DISTINCT a.city AS city
RETURN city
ORDER BY city

-- UNWIND literal lists.
UNWIND [1, 2, 3] AS n
RETURN n, n + 10 AS shifted

-- UNWIND with row-dependent CREATE property values.
UNWIND ["Joe", "Bob", "Ada"] AS name
CREATE (p:Person {name: name, slug: name + "-person"})
RETURN p.name, p.slug

-- MERGE with row-dependent property values.
UNWIND ["Joe", "Bob"] AS name
MERGE (p:Person {name: name})
RETURN p.name

-- SET scalar properties.
MATCH (p:Person)
WHERE p.name = "Joe"
SET p.age = 35, p.updated = true
RETURN p.name, p.age, p.updated

-- SET map merge.
MATCH (p:Person)
WHERE p.name = "Joe"
SET p += {city: "Hamburg", score: 10}
RETURN p.name, p.city, p.score

-- SET map replacement.
MATCH (p:Person)
WHERE p.name = "Joe"
SET p = {name: "Joe", city: "Berlin"}
RETURN p.name, p.city

-- REMOVE property.
MATCH (p:Person)
WHERE p.name = "Joe"
REMOVE p.city
RETURN p.name, p.city

-- REMOVE label.
MATCH (p:Person)
WHERE p.name = "Joe"
REMOVE p:Person
RETURN p

-- DELETE relationship.
MATCH (:Person)-[r:KNOWS]->(:Person)
DELETE r
RETURN "deleted"

-- DETACH DELETE node.
MATCH (p)
WHERE p.name = "Bob"
DETACH DELETE p
RETURN "deleted"

-- Parameter examples for C API or future UI parameter binding.
MATCH (p:Person)
WHERE p.name = $name
RETURN p

CREATE (p:Person {name: $name, age: $age})
RETURN p

UNWIND $names AS name
CREATE (p:Person {name: name})
RETURN p.name

-- Procedure call with aliases.
MATCH (a)
CALL randomWalk(a, 5, 42) YIELD node AS visited
RETURN visited
LIMIT 10

-- UNION with compatible column names.
MATCH (p:Person)
RETURN p.name AS name
UNION
MATCH (c:Company)
RETURN c.name AS name

-- UNION ALL keeps duplicates.
MATCH (p:Person)
RETURN p.city AS value
UNION ALL
MATCH (p:Person)
RETURN p.city AS value
