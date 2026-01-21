# Nautylus CLI

The `nautylus` binary is a local, in-process demo that loads a dataset,
runs vector + graph queries, and serves a minimal UI.

## Build

```sh
make
```

## Demo dataset

```sh
./nautylus demo
./nautylus query --node alba --k 3
./nautylus serve --port 6180
```

## CSV input

Nodes CSV: `name,v0,v1,...`

```csv
alba,0.1,0.2,0.3
boreal,0.2,0.1,0.4
```

Edges CSV: `from,to,weight`

```csv
alba,boreal,1.0
```

Load + query:

```sh
./nautylus load --nodes nodes.csv --edges edges.csv --dim 3 --out demo.nty
./nautylus query --db demo.nty --node alba --k 3
./nautylus serve --db demo.nty
```

## JSON input

```json
{
  "nodes": [
    {"name": "alba", "vector": [0.1, 0.2, 0.3]}
  ],
  "edges": [
    {"from": "alba", "to": "alba", "weight": 1.0}
  ]
}
```

```sh
./nautylus query --json data.json --node alba --k 3
```

## Notes

- The UI uses the locally bundled `resources/d3/d3.v7.min.js`.
- The `.nty` store format is a demo snapshot and not stable yet.
