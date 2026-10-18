# Credits and source provenance

This repository is a derivative of the open-source MiniPRO project, not an
independently authored programmer implementation.

* Original author: **Valentin Dudouyt**, as recorded in the source headers
  and original Git history.
* Upstream maintainer: **David Griffith** and the contributors to
  [DavidGriffith/minipro](https://gitlab.com/DavidGriffith/minipro).
* Immediate upstream: **Walter Haslbeck**,
  [whaslbeck/minipro](https://github.com/whaslbeck/minipro), including the
  T48 bit-banging and power-control implementation on which this work builds.
* Starting revision: `cfe2171ca1b23399558b8d2a603a2ac3fa3faf5d`.
* SEEQ-specific changes published and maintained by **ofgod1974**, with
  AI-assisted implementation: the experimental 52B33 T48 algorithm,
  device profile, protocol tests, Windows build helper and Italian guide.

The original Git history, source copyright notices, attribution section and
GPL license are retained. This list supplements the existing notices and is
not an exhaustive list of contributors. See [LICENSE](LICENSE) and the
per-file headers for the applicable terms. The new SEEQ driver is provided
under GPL-3.0-or-later.

The programming sequence was derived from the SEEQ Data Book 1988,
52B33 datasheet (October 1987), printed pages 1-11 through 1-16. Datasheet
documentation does not constitute manufacturer approval of this implementation.

**Validation status:** software/protocol tests pass; the SEEQ algorithm has
not been electrically validated with a real T48 and 52B33. This fork is
experimental and is not an official release of either upstream project.
