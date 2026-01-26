# Universal Type Ontology (Future)

This document captures desired properties and open questions for a cross-agent, shared type ontology.

## Desired properties

- **Shared semantics**: types mean the same thing across toolchains and agents.
- **Stable identifiers**: a canonical ID or hash for each type definition.
- **Versioning**: forward-compatible evolution without ambiguity.
- **Deterministic layout**: machine-checkable representation with reproducible hashing.
- **Capability awareness**: types can express or require capabilities where needed.
- **Proof-friendly**: encodings are amenable to SMT reasoning in the MVP fragment.
- **Composability**: can reference or embed domain ontologies without conflicts.
- **Minimal surface**: start with a small core (primitives, records, enums, vectors).

## Open questions

- How are type IDs derived (content hash vs registry)?
- How do we encode versioning (semantic versions vs hash-suffixed IDs)?
- What is the minimal core needed for bundle interchange?
- How are refinements and contracts attached to shared types?
- How are capability requirements represented in the ontology?
- How do we express ownership/linearity and resource constraints?
- What is the expected trust model for third-party ontologies?
- How do we prevent ontology drift across agents and tools?
