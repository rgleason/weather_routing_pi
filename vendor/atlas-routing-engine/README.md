# Atlas routing engine (experimental)

A standalone sixth engine: continuous physical connection solving on adaptive
spatial decisions, with coarse time-dependent Bellman feedback and explicit
arrival/wait/mode histories. It does not call the five existing search engines.

See [API/build instructions](../../docs/atlas-engine.md),
[mathematics](../../research/sixth/mathematics.md),
[benchmarks](../../research/sixth/results.md) and
[literature](../../research/sixth/literature.md).

The engine uses the adjacent shared request/provider/result types and
unchanged physical operations/independent validator. No GUI is required.
Experimental finite discretisations and work caps do not establish
completeness, global optimality or continuous-world navigation safety.
Default production routing selection is unchanged.

Code in this directory is GPL-3.0-or-later; shared dependencies retain their
own licences. Linux is the qualified standalone benchmark platform for this
research run; Windows/macOS runtime and live chart-provider qualification are
not inferred from the Linux results.
