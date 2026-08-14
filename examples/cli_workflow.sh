#!/bin/sh
set -eu

DB=${1:-examples-workflow.ng}

make

./build/nautylus create "$DB"

./build/nautylus query "$DB" \
  'CREATE (joe:Person {name: "Joe", age: 34, city: "Berlin"}),
          (bob:Person {name: "Bob", age: 31, city: "Berlin"}),
          (joe)-[:KNOWS {since: 2020}]->(bob)
   RETURN joe, bob' \
  --format verbose

./build/nautylus query "$DB" \
  'MATCH (a:Person)-[r:KNOWS]->(b:Person)
   RETURN a.name, r.since, b.name
   ORDER BY a.name' \
  --format verbose

./build/nautylus query "$DB" \
  'MATCH (a:Person)
   OPTIONAL MATCH (a)-[:WORKS_AT]->(c:Company)
   RETURN a.name, c.name
   ORDER BY a.name' \
  --format json

./build/nautylus query "$DB" \
  'MATCH (a:Person)
   RETURN a.city, count(a) AS people
   ORDER BY people DESC' \
  --format verbose

./build/nautylus stats "$DB"
./build/nautylus validate "$DB"
