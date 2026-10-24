# Authored backdrop depth

The depth regions follow original object silhouettes. Every region has an
`object` label in `backdrop-layers.json`; later regions overwrite earlier ones,
allowing recessed openings and complete foreground objects to remain coherent.
Depth is an authored interpretation of the flat art, not a recovered 3D scene.

| Backdrop | Far (7 m) | Middle (4.6 m) | Near (3.2 m) |
| --- | --- | --- | --- |
| buckroom | Wall and wall-mounted art | Shelves, workstation and tower, dresser and lamp, cabinet | Complete chair, sofa, foreground platform and floor |
| room | Complete recessed window/door openings | Wall, frames and skirting | Floor |
| pastoral | Landscape and recessed doorway opening | Entire arch frame and attached staircase | Foreground foliage and ground |
| field | Sky | Distant hill/bush skyline | Whole architecture, foreground shrubs, meadow and leaf plant |
| space | Stars, distant planets and upper spacecraft | Complete yellow planets and lower-left planet arc | Complete right spacecraft/exhaust and continuous terrain, including spires and rocks |
| clouds | Entire continuous cloud photograph | — | — |
| yellow | Entire uniform field | — | — |

The source-coordinate landmarks in `tests/backdrop-landmarks.json` guard object
ownership, including the pastoral arch crown, jambs, treads, rail, front newel
and recessed opening. Tests also check that every generated layer matches the
installed/imported catalog and that recomposition is pixel-identical to the
original art. A region boundary must not run through a single coherent object
merely to create another depth band.

Hidden pixels still use nearest-source-pixel extrapolation from their own
layer. This preserves the original artwork and avoids repeating foreground
objects in the background, but large head movement can expose stretched fill.
These masks do not invent unseen scenery or reconstruct a continuous depth
surface inside a single illustrated object.
