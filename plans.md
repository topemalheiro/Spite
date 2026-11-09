1. **Device view setting:** not done.
   * Devices shown as a horizontal chain at the bottom, like Live's device view, instead of the vertical list in each strip.
   * Devices as modules that collapse and enlarge with an arrow.
   * An action that hides the strips' device lists and faders on the Cue page. The agent is supposed to ask you exactly what to hide.
2. **Views menu next to View:** not done. You'd save and edit your own views (which panels, sidebars, strips and device layout each page shows), then switch or cycle between them.
3. **Later, after checking with you:**
   * a modular rack view on the Cue page, off by default
   * an automation view tied to step sequencers
   * preset slots beside devices that a control-surface button can trigger
   * a coloured box showing what a control surface has mapped
4. **Max for Live devices in PgD4S, next:**
   * **Snake Mod Super:** step in time with Spite's transport (a tempo-synced step clock for `live.step`/`transport`, Spite sending PgD4S the song position, its note tables). Then integrate it more into the DAW: the Custom lanes driving devices' parameters and automation instead of the Live API. Its Snake Shapes pictures are in the other Snake devices (`Snake Mod Super 2 - Beta.amxd`, `Snake with SS/...`).
   * **Oscilloscope's Popup:** decide what it opens inside Spite: the plugin's window, or an overlay over the face.
   * **Slink, `gen~`, `mc.*` and JavaScript:** see them in plugdata first, then look for Pd/plugdata packages that cover them (or build an alternative to `gen~`).
   * **Faces:** numbers as Live shows them ("5.0", units like "dB"), and faces for plugins other than PgD4S.
5. **Automation:** it's been bugging more and more. Go through what goes wrong with you, then fix it.
