<?php
declare(strict_types=1);

require __DIR__ . '/Nautylus.php';

$db = 'php-binding.ng';
@unlink($db);

$graph = NautylusGraph::create($db);
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

echo $graph->query('MATCH (a:Person)-[r:KNOWS]->(b:Person) RETURN a.name, r.since, b.name');
echo $graph->query('CREATE (ada:Person {name: "Ada"}) RETURN ada.name', true);

$graph->save();
$graph->close();

$opened = NautylusGraph::open($db);
echo 'nodes=' . $opened->nodeCount() . ' relationships=' . $opened->relationshipCount() . PHP_EOL;
$opened->close();
