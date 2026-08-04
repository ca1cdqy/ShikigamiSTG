# Documentation Contributions

The Chinese documentation is the authoring source. Use this workflow:

1. Update the Chinese page under `docs/zh` first.
2. Keep heading levels, code examples, and links stable.
3. Translate the content into the matching path under `docs/en`.
4. Link to Doxygen instead of copying API signatures into tutorials.
5. Put code examples in compilable tests or examples and reference the verified version from the documentation.

Doxygen comments are written once in English because they define the single C++ API contract and must not have duplicated source-language variants.
