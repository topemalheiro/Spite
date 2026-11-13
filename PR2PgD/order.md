
| step | PR                                            | desktop | why this step                                                 |
| ---- | --------------------------------------------- | ------- | ------------------------------------------------------------- |
| 1    | plugdata-pd: Detective Mode trace hook        | Spite 1 | library PRs go first, because three plugdata PRs wait on them |
| 1    | plugdata-pd: save a big canvas in linear time | Spite 4 |                                                               |
| 1    | JUCE: Linux maximize                          | Spite 2 |                                                               |
| 2    | plugdata: the 5 small GUI fixes               | Spite 3 | small and independent, quick to review                        |
| 2    | plugdata: Lua link flags, hidden-window fix   | Spite 4 |                                                               |
| 3    | plugdata: plugin-host stalls fix              | Spite 4 | needs the pure-data canvas PR                                 |
| 3    | plugdata: Linux maximize                      | Spite 2 | needs the JUCE PR                                             |
| 4    | plugdata: Detective Mode                      | Spite 1 | needs the hook PR; it's the biggest review, so it goes last   |
