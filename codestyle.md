# AGENTS.md

## Primary goal

Write code for humans first.

Optimize for readability, maintainability, and ease of debugging.

Do not optimize for minimum line count.
Optimize for minimum cognitive load.

When multiple implementations are equally correct, choose the one that is
easiest for another engineer to understand six months from now.

## General coding style

- Prefer simple, explicit code over clever code.
- Prefer boring, predictable solutions over novel or overly abstract ones.
- Use descriptive names for variables, functions, classes, types, and files.
- Avoid abbreviations unless they are obvious and standard in the codebase.
- Keep functions small and focused on one responsibility.
- Keep control flow easy to follow.
- Prefer early returns over deeply nested conditionals.
- Avoid deeply nested loops and branches when a simpler structure is possible.
- Avoid dense expressions that require careful parsing.
- Avoid unnecessary one-liners.
- Split complicated expressions into clearly named intermediate variables.
- Prefer explicit steps over compressed logic.
- Do not introduce abstractions unless they clearly reduce complexity.
- Do not create helper functions that are only used once unless they
  meaningfully improve readability.
- Do not introduce unnecessary design patterns.
- Do not add unnecessary layers of indirection.
- Avoid premature optimization.
- Prefer standard language and framework features over custom machinery.
- Match the existing conventions of the repository whenever reasonable.

## Naming

Use names that communicate intent.

Bad:

    const d = users.filter(x => x.a);

Better:

    const activeUsers = users.filter(user => user.isActive);

Avoid vague names such as:

- data
- value
- thing
- item
- obj
- tmp
- result

These names are acceptable only when their meaning is immediately obvious
from a very small scope.

Functions should generally describe what they do.

Prefer:

    calculateInvoiceTotal()
    findUserByEmail()
    removeExpiredSessions()

Avoid vague names such as:

    process()
    handle()
    execute()
    doStuff()

unless the surrounding abstraction makes their purpose obvious.

## Functions

Functions should have one clear purpose.

Prefer:

- short functions
- obvious inputs
- obvious outputs
- limited side effects
- straightforward control flow

Avoid functions that:

- perform several unrelated tasks
- mutate many unrelated values
- mix business logic with infrastructure unnecessarily
- require many boolean parameters
- depend on hidden global state
- contain large nested condition trees

If a function becomes difficult to understand, simplify it before extracting
abstractions mechanically.

Do not split code into tiny functions merely to reduce line count.

The goal is readability, not the smallest possible function size.

## Control flow

Prefer straightforward control flow.

Prefer:

    if (!user) {
        return null;
    }

    if (!user.isActive) {
        return null;
    }

    return buildProfile(user);

over deeply nested equivalents.

Avoid clever boolean expressions when separate conditions are easier to read.

Prefer:

    const hasValidEmail = user.email !== null && user.email !== "";
    const canReceiveNotifications = user.notificationsEnabled;

    if (hasValidEmail && canReceiveNotifications) {
        sendNotification(user);
    }

over packing unrelated checks into a difficult expression.

## Variables

Use intermediate variables when they make complex logic easier to understand.

Prefer:

    const subtotal = calculateSubtotal(items);
    const tax = calculateTax(subtotal);
    const total = subtotal + tax;

over:

    const total = calculateSubtotal(items) + calculateTax(calculateSubtotal(items));

Do not create intermediate variables that merely rename something without
adding clarity.

## Comments

Comments should explain why something exists, not narrate obvious code.

Bad:

    // Increment count
    count++;

Good:

    // The API uses a 1-based page index while our internal pagination is 0-based.
    const apiPage = page + 1;

Use comments for:

- non-obvious business rules
- compatibility constraints
- intentional tradeoffs
- surprising behavior
- external system limitations
- important invariants

Do not add comments to every function or every block.

Readable code should usually explain itself.

## Error handling

Handle errors explicitly.

- Do not silently swallow exceptions.
- Preserve useful error context.
- Use actionable error messages.
- Avoid large catch-all exception handlers unless appropriate.
- Do not use exceptions for normal control flow.
- Validate assumptions at appropriate boundaries.

Prefer errors that help the next engineer understand what failed and why.

## Types

When the language supports types:

- prefer specific types over overly generic types
- avoid `any` or equivalent escape hatches unless necessary
- model meaningful domain concepts explicitly
- avoid extremely complex type-level programming unless it provides clear value
- choose understandable types over clever type tricks

Types should make the code easier to understand, not harder.

## Data structures

Choose the simplest data structure that fits the problem.

Do not introduce custom classes or elaborate object hierarchies when a plain
object, struct, list, map, or set is sufficient.

Use domain-specific structures when they meaningfully prevent mistakes or
clarify intent.

## Abstraction

Do not abstract code simply because two pieces of code look similar.

Create an abstraction when the pieces share the same meaningful concept and
are likely to evolve together.

A small amount of duplication is preferable to the wrong abstraction.

Avoid speculative abstractions created for hypothetical future requirements.

Implement what is needed now while keeping the code easy to change.

## Architecture

Respect the existing architecture unless there is a strong reason to change it.

Do not perform broad architectural refactors as a side effect of a small task.

Keep changes scoped to the requested work.

When architectural changes are necessary:

1. choose the simplest viable design
2. minimize new concepts
3. preserve clear module boundaries
4. keep dependencies understandable
5. avoid unnecessary coupling

## Dependencies

Do not add a new dependency if the task can be implemented clearly with
existing dependencies or standard library functionality.

When a new dependency is genuinely useful:

- prefer mature and well-maintained libraries
- use the smallest dependency that solves the problem
- avoid dependencies for trivial functionality
- follow the repository's existing dependency conventions

## Existing codebase

Before implementing a change:

1. inspect nearby code
2. identify existing conventions
3. look for similar implementations
4. reuse established patterns where appropriate
5. avoid inventing a second way to solve a problem the repository already
   solves consistently

Prefer consistency with good existing code over imposing a new personal style.

If existing code is difficult to read, do not blindly reproduce its worst
patterns. Improve readability locally without introducing unrelated churn.

## Scope

Make the smallest coherent change that fully solves the task.

Do not:

- refactor unrelated files
- rename unrelated symbols
- reformat large areas unnecessarily
- rewrite functioning code without a reason
- introduce speculative features
- add configuration that is not needed

Keep diffs focused and reviewable.

## Tests

Add or update tests when behavior changes.

Tests should be readable too.

Prefer tests that clearly communicate:

- the scenario
- the action
- the expected result

Avoid excessive mocking when a simpler test is possible.

Do not test implementation details unnecessarily.

Test meaningful behavior and important edge cases.

## Generated code quality

Do not produce code that merely appears sophisticated.

Avoid common AI-generated-code problems such as:

- excessive abstraction
- unnecessary wrapper classes
- generic helper utilities with unclear value
- repeated defensive checks for impossible states
- verbose comments explaining obvious syntax
- huge configuration objects without justification
- unnecessary factories
- unnecessary interfaces
- unnecessary dependency injection
- premature extensibility
- duplicated validation at every layer
- excessive error wrapping
- clever functional chains that are harder to debug than normal control flow

Favor code that an experienced engineer would be comfortable maintaining.

## Implementation workflow

Before writing code:

1. understand the requested behavior
2. inspect relevant existing code
3. identify the simplest implementation
4. consider important edge cases

Then implement the change.

After implementation:

1. reread the entire diff
2. look specifically for unnecessary complexity
3. simplify clever or dense code
4. improve unclear names
5. remove unnecessary abstractions
6. remove unnecessary comments
7. remove duplicated or dead code
8. verify error handling
9. verify relevant tests
10. make sure the result matches existing repository conventions

Do not treat the first implementation as final.

Perform a readability pass before finishing.

## Final readability check

Before completing any coding task, ask:

- Could this be written more simply?
- Are the names immediately understandable?
- Is any abstraction unnecessary?
- Is any logic unnecessarily compressed?
- Is the control flow obvious?
- Are there too many levels of indirection?
- Would a new engineer understand this without an explanation?
- Did I change anything unrelated to the task?
- Would I be comfortable debugging this code six months from now?

If the answer reveals unnecessary complexity, simplify the code before
finishing.

## Core rule

Write code as if it will be maintained by humans for years.

Correctness comes first.
Clarity comes immediately after.
Cleverness is not a goal.
