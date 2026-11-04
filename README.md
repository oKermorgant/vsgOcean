# vsgOcean

**An experimental ocean scene renderer for marine robotics, built on VulkanSceneGraph.**

> :warning: **Status: ongoing, experimental project.** vsgOcean is under active development: APIs will change, features are incomplete, and rendering is a baseline that will be refined. It is not ready for production use. Feedback and contributions are welcome, but expect churn.

vsgOcean renders marine scenes — terrain, surface vehicles and underwater robots — with an environment designed for simulation. The light transport is driven by physical water parameters (the Jerlov taxonomy, a bio-optical composition), the wave models are inspired by [asv_wave_sim](https://github.com/srmainwering/asv_wave_sim) (a Gazebo wave plugin), and every layer is settable at runtime — by the GUI, the command line, or the C++ API.

In short:

- **Scene model**: named, nestable `Model`s with frames addressable by name and movable at runtime through `setPose()` — the seam for driving visuals from a dynamics simulator; SDF worlds and models load natively *(optional, [sdformat](https://github.com/gazebosim/sdformat))*.
- **Sky and weather**: analytic sky with a sun-driven day/night cycle (moonless, dark nights — no ambient floors), hemisphere-dependent constellations, cloud cover on the okta scale, rain (falling streaks, splash rings, a roughened sea).
- **Ocean surface**: Gerstner component waves and an ECKV FFT sea, both wave models inspired by [asv_wave_sim](https://github.com/srmainwering/asv_wave_sim) (a Gazebo wave plugin), with a detail cascade and Jacobian whitecap foam; wind-driven ripple detail; sky reflection, glitter and glare above, the Snell window below.
- **Underwater rendering**: per-pixel, per-channel Jerlov attenuation over the true submerged view segment, the light on the geometry filtered by its own depth (the sun does not reach a deep wreck - the lamps do), Henyey-Greenstein in-scatter integrated along the view path, an underwater sky, volumetric lamp cones, marine snow and a turbidity-driven depth-of-field blur. The surface's through-view is per pixel: the sea covers what it actually hides - a shallow shore reads through the attenuating water, a deep wreck and its lamps never shine through.
- **Dimmable lights**: an SDF `<light>` intensity is each lamp's ceiling; lamps cluster by aim direction into per-robot light groups (one face, one control) — shading, cones and snow sparkle all follow.
- **Annotation ground truth**: a semantic pass renders the models with each pixel carrying its model's ID (read back per pixel, `--semantic` in the example writes the ID image and prints the derived boxes) - the base for training-data generation. At runtime the **R** key switches the view between the photo, the annotation and a 50% overlay; the display colour of a model follows its name, deterministically.
- **Render pipelines**: the scene renders from any number of cameras (sensor cameras to texture, ready for image readback); `--headless` renders offscreen without a window, for servers, CI and non-presenting GPUs.

## Videos

[![The ocean surface: the wind builds a foam-capped FFT sea while the water type steps from oceanic to coastal](https://github.com/user-attachments/assets/4b821752-acc9-4fc9-8505-bbc6754527d7)](https://github.com/user-attachments/assets/4b821752-acc9-4fc9-8505-bbc6754527d7)

[![Underwater: an ROV at a ship wreck, its dive lamps dimmed to a quarter of their SDF intensity, the water stepping from oceanic to coastal (the picture-in-picture is its own camera)](https://github.com/user-attachments/assets/f06d16ad-1c0f-4c85-80ff-6aadfeeea3d0)](https://github.com/user-attachments/assets/f06d16ad-1c0f-4c85-80ff-6aadfeeea3d0)

*(still frames: [`img/surface.png`](img/surface.png), [`img/underwater.png`](img/underwater.png))*

## Core rendering: the water colour and the waves

The look lives in two parameter families, both runtime-tunable.

### Water colour and light transport

The water is described either by a **Jerlov water type** (the classical marine-optics taxonomy: oceanic I, IA, IB, II, III, then coastal 1–9 — open ocean to murky harbour) or directly by its **bio-optical composition**: chlorophyll [mg/m³], coloured dissolved organic matter (CDOM absorption at 440 nm [1/m]) and suspended sediment [g/m³]. The composition drives a simple bio-optical model producing the per-channel diffuse attenuation, the scattering coefficient and the sub-surface colour, from which everything follows: the underwater attenuation and in-scatter (per-channel Beer–Lambert along the true submerged view segment, Henyey–Greenstein forward scattering g = 0.924), the water's own colour seen from above, the depth-of-field blur and the lamp cones. The 14 Jerlov types are presets of the composition space; the default is coastal 3 (the western English Channel).

### Waves

The default sea is a **Gerstner component wave field**, a wave model inspired by [asv_wave_sim](https://github.com/srmainwering/asv_wave_sim) (a Gazebo wave plugin): it follows its component spreading (8 octave-spaced components at 36°), giving an irregular, short-crested sea. The mean component (amplitude, period, steepness), the spreading (count, scale, angle) and the wind are all runtime parameters, and the field can also be evaluated on the CPU (`Wavefield::height`), not only rendered. With `--foam` the component sea grows **Jacobian whitecaps** too: the component field never truly folds, so the crest compression is remapped by a cubic wind factor — the coverage rises steeply with the wind, from nothing at Beaufort 2–3 to broad whitecap streaks near gale.

The alternative **FFT sea** (`--fft`) synthesises an ECKV unified directional spectrum by inverse FFT, after the linear random FFT model of asv_wave_sim, with a choppiness parameter (horizontal displacement whose folds produce **Jacobian whitecap foam** with `--foam`), a wave-age parameter and an optional second, finer cascade (`--fft-detail`) that kills tile repetition and adds detail.

### References

- N. G. Jerlov, *Marine Optics*, Elsevier, 1976 — the water type taxonomy.
- L. de Gerstner (1809), *Theorie der Wellen* — the trochoidal wave model.
- W. J. Pierson & L. Moskowitz, *A proposed spectral form for fully developed wind seas…*, J. Geophys. Res., 1964 — the wind-sea spectrum asv_wave_sim samples.
- T. Elfouhaily, B. Chapron & D. Katsaros, *A unified directional spectrum for long and short wind-driven waves*, J. Geophys. Res., 1997 — the ECKV spectrum of the FFT sea.
- J. Tessendorf, *Simulating Ocean Water*, 2001 — the inverse-FFT synthesis and the Jacobian foam.
- L. C. Henyey & J. L. Greenstein, *Diffuse radiation in the galaxy*, ApJ, 1941 — the forward-scattering phase function.
- [asv_wave_sim](https://github.com/srmainwering/asv_wave_sim) (R. Mainwaring) — the component wave and linear random FFT models vsgOcean's wave fields are inspired by.

## The example

A second, minimal binary lives next to it: **`vsgOcean_tutorial`** (`example/src/tutorial.cpp`) - the smallest complete marine scene in one readable `main`, no command line, no widgets: a window, an SDF scene, a few environment setters, a camera, the render loop, and a boat placed on the waves through `Scene::surfaceHeight` + `setPose` - the seam a simulator plugs into. Start there when you want to use vsgOcean from C++.

The example application renders an islands scene with a surface vehicle (a WAM-V), an ROV (BlueROV2) circling a ship wreck with its dive lamps, and a floating wind turbine — animated in real time on the waves, with the environment fully tunable at runtime. Two sensor cameras (the ROV's nose camera and a boat camera) render to texture and preview in the panel.

```bash
# every flag is one --help away: capture, diagnostics, every wave and water parameter
# (the standard vsg viewer flags apply as well)
./vsgOcean_example --help

# the default scene: islands + wamv + bluerov2 + wreck, static sun (35 deg, 135 deg azimuth)
./vsgOcean_example

# custom scenes: SDF files passed as positional arguments
./vsgOcean_example my_world.sdf
```

### Runtime controls

The environment is tuned at runtime through several widgets (`vsgOcean::EnvironmentControls`): sky, ocean (surface), underwater, render, lights.

| input | action |
| --- | --- |
| Left drag / right drag / wheel | Translate / orbit (stable horizon) / dolly |
| C | Camera previews on / off (hidden by default; says so when no camera is rendering) |
| H | Help window on / off (the bindings, the model tracking, the live stats) |
| R | Rendering view: photo, the annotation (each model in the colour of its name), the annotation at 50% over the photo |
| O | Ocean surface widget on / off (wind, waves) |
| M | Model tracking: cycle the model the camera follows |
| U | Underwater widget on / off (water optics, current, snow, DOF blur) |
| L | Lights widget on / off (one intensity slider per light group) |
| E | Export the environment (the YAML target is typed or picked in the help window) |
| S | Sky widget on / off (sun, day cycle, fog, cloud cover, rain) |
| Esc | Quit |

The bindings are always visible in a small HUD in a corner of the window. When vsgOcean is built with vsgImGui, each widget is its own window: the widgets read the environment every frame and write only on change — an update from outside (a topic, the code) is reflected immediately, and changing a widget takes over from any preset.
The best is probably to see what widget / slider impacts a given visual property.

### Saving and reloading an environment

An environment tuned in the widgets can be exported to a YAML file (**E**) — sky and weather, wind, water optics and wave.
Command-line argument **`--env`** loads it back at startup, and every group and key is optional, so hand-written files can be partial:

```yaml
# storm.yaml, by someone in a hurry
wind:
  speed_m_s: 16
sky:
  cloud_cover_oktas: 7
  rain_rate: 0.8
waves:
  model: fft
  foam: 0.6
```

```bash
./vsgOcean_example --env storm.yaml
```

The file is applied first, so any explicit command line flag still overrides its key (`--env storm.yaml --rain 0` for a dry storm). The full key list is documented in [`include/vsgOcean/core/EnvironmentFile.h`](include/vsgOcean/core/EnvironmentFile.h); the support is built when [yaml-cpp](https://github.com/jbeder/yaml-cpp) is found (a common system package).

## How vsgOcean sits on its dependencies

Everything vsgOcean builds — the wave surfaces, the sky, the underwater passes — is a plain [VulkanSceneGraph](https://github.com/vsg-dev/VulkanSceneGraph) scene graph, and its public API speaks vsg types (`vsg::ref_ptr`, `vsg::dvec3` poses, `vsg::Options`), so **vsg is the one dependency a consumer links and sees**. The scene description side is [sdformat](https://github.com/gazebosim/sdformat): when it is found at build time, `SDFLoader` turns SDF worlds and models into vsg graphs, but the link is private — an application that loads SDF files never sees an sdformat header. Mesh and texture files referenced by a scene load through [vsgXchange](https://github.com/vsg-dev/vsgXchange) readers, which the application registers in `vsg::Options`; vsgOcean calls them, it does not link them.

```
      ┌────────────────────────────────────────────────────────────┐
      │                    the application                         │
      │           (the example, the tutorial, a simulator)         │
      │    drives poses, lights and the environment through the    │
      │    C++ API, and links vsgOcean + vsgXchange                │
      └────────────────────────────┬───────────────────────────────┘
                                   │
      ┌────────────────────────────┴─────────────┐   ┌──────────────────────┐
      │                 vsgOcean                 │   │      vsgXchange      │
      │  core      Environment, Wavefield,       │   │  readers for mesh,   │
      │            FftWaves, WaterOptics         │   │  texture and image   │
      │  scene     Models, Scene,                │──▶│  files (glTF, OBJ,   │
      │            RenderPipeline, Sky, Rain,    │   │  PNG, …) — shared    │
      │            MarineSnow, Underwater,       │   │  through vsg::Options│
      │            SDFLoader                     │   └──────────────────────┘
      │  semantic  the ID pass and its palette   │
      │  ui        controls, widgets, HUD        │
      └──────┬───────────┬───────────┬───────────┘
             │           │           │
             ▼           ▼           ▼
   ┌─────────────┐ ┌───────────┐ ┌──────────────────────────────┐
   │     vsg     │ │ sdformat  │ │ vsgImGui  yaml-cpp  ZLIB     │
   │ required,   │ │ optional, │ │ optional, private: widgets,  │
   │ public:     │ │ private:  │ │ YAML environment files, PNG  │
   │ scene graph,│ │ SDF       │ │ screenshots — each built     │
   │ Vulkan      │ │ worlds    │ │ only when found at configure │
   └──────┬──────┘ └───────────┘ └──────────────────────────────┘
          │
          ▼
       Vulkan (a Vulkan-capable GPU and driver)
```

The option flags of the build section map to this: 

- `-DVSGOCEAN_WITH_SDF=OFF` removes the sdformat arrow
- `-DVSGOCEAN_WITH_GUI=OFF` the vsgImGui one
- `-DVSGOCEAN_WITH_ENV_FILE=OFF` the yaml-cpp one

## Building

Dependencies:

- C++17, CMake ≥ 3.22
- [VulkanSceneGraph](https://github.com/vsg-dev/VulkanSceneGraph) (vsg) ≥ 1.1
- [vsgXchange](https://github.com/vsg-dev/vsgXchange) (model loading)
- [sdformat](https://github.com/gazebosim/sdformat) ≥ 16 *(optional)* — SDF support is enabled automatically when sdformat is found; build with `-DVSGOCEAN_WITH_SDF=OFF` to force it off
- [vsgImGui](https://github.com/vsg-dev/vsgImGui) *(optional)* — the environment tuning GUI is enabled automatically when vsgImGui is found; build with `-DVSGOCEAN_WITH_GUI=OFF` to force it off
- [yaml-cpp](https://github.com/jbeder/yaml-cpp) *(optional)* — the YAML environment files (the E export / `--env` load) are enabled automatically when yaml-cpp is found; build with `-DVSGOCEAN_WITH_ENV_FILE=OFF` to force it off
- A Vulkan-capable GPU and driver

```bash
cmake -B build .
cmake --build build -j
./build/example/vsgOcean_example
```

## Roadmap

- Underwater rendering refinements
    - god rays
    - wave-field caustics on geometry
- Surface refinements: planar reflection / refraction passes of the actual scene; foam persistence; subsurface scattering; screen-space reflections; exposure / tone mapping; sun shadow map on the water
- Middleware integration is kept out of this library on purpose — poses and environment updates come from the outside, through the C++ API

## Acknowledgments and third-party notices

vsgOcean is MIT-licensed. The following third-party work is included or derived, with grateful acknowledgment:

- **[asv_wave_sim](https://github.com/srmainwering/asv_wave_sim)** (© 2019 Rhys Mainwaring, [Apache-2.0](http://www.apache.org/licenses/LICENSE-2.0)): the `Wavefield` wave model (Gerstner component waves, Pierson–Moskowitz sampling, deep water dispersion) is derived from `asv_wave_sim`, with modifications.
- **[UUV Simulator](https://github.com/uuvsimulator/uuv_simulator)** (© 2016 The UUV Simulator Authors, [Apache-2.0](http://www.apache.org/licenses/LICENSE-2.0)): the Gerstner surface displacement and normal formulation in `shaders/ocean.vert` is derived from its wave shaders, with modifications.
- **[VulkanSceneGraph](https://github.com/vsg-dev/VulkanSceneGraph)**, **[vsgXchange](https://github.com/vsg-dev/vsgXchange)** (© Robert Osfield, MIT) and **[sdformat](https://github.com/gazebosim/sdformat)** (© Open Source Robotics Foundation, Apache-2.0): the rendering and scene description stack vsgOcean builds on.
- The interleaved gradient noise dithering used in the shaders follows the technique of Jorge Jimenez, *"Next Generation Post Processing in Call of Duty: Advanced Warfare"* (2014).
### Example model assets

The example scene ships third-party models, each with its own origin and license; they are example data, not part of the library. Per-model attribution also travels with the model files.

| Model | Origin | License |
|---|---|---|
| `wamv` | the WAM-V of [VRX](https://github.com/osrf/vrx) (© Open Source Robotics Foundation), taken through [asv_wave_sim](https://github.com/srmainwaring/asv_wave_sim)'s gz-waves-models, textures downscaled to 1024 | [Apache-2.0](http://www.apache.org/licenses/LICENSE-2.0) |
| `bluerov2` | geometry derived from the official Blue Robotics [BlueROV2](https://bluerobotics.com/store/rov/bluerov2/) CAD (including the Ping360 sonar) | © Blue Robotics, all rights reserved. Not covered by vsgOcean's MIT license; redistributed for research visualisation with attribution |
| `herkules_ship_wreck` | the M/S Herkules wreck of [UUV Simulator](https://github.com/uuvsimulator/uuv_simulator) (© 2016 Robert Bosch GmbH) | [Apache-2.0](http://www.apache.org/licenses/LICENSE-2.0) |
| `islands` | the island terrain of the [osgOcean](https://github.com/kbale/osgocean) example resources | [LGPL-3](https://www.gnu.org/licenses/lgpl-3.0.html) |
| `floatgen` | modelled in-house after the FLOATGEN demonstrator (Ideol "Damping Pool" foundation, Vestas V80-2.0MW turbine) from its published dimensions; the marine growth on the caisson is procedural | MIT, as vsgOcean |

The `attenuation` example model is built from plain SDF primitives and carries no third-party content.

A large part of the code writing was carried out with agentic AI coding assistants: **[GitHub Copilot](https://github.com/features/copilot)** and **[Mistral Vibe](https://mistral.ai)**.

Projects consulted as rendering references but **not** included: [Stonefish](https://github.com/patrykcieslak/stonefish) (GPL-3), [Vulkan-Spectral-Ocean](https://github.com/micaalle/Vulkan-Spectral-Ocean) (MIT). [osgOcean](https://github.com/kbale/osgOcean) is listed above: its island data is included as example assets.

Per-file attribution is kept in the headers of the derived sources (`src/core/Wavefield.cpp`, `shaders/ocean.vert`).
