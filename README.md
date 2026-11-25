# ClaudeClipper

**HardEdge Clipper** — ein VST3-Audio-Plugin (mit Standalone-App) auf Basis von [JUCE](https://juce.com) 8.0.6.
Der Clipper arbeitet mit hart/weich wählbarer Kennlinie, optionalem Oversampling und Latenzkompensation des Dry-Signals.

## Features

- 4 Clipper-Kennlinien: **Hard**, **Soft** (Knie mit tanh-Übergang), **Cubic**, **Sine**
- **Oversampling**: Off / 2x / 4x / 8x / 16x (latenzkompensiert, Dry-Signal wird passend verzögert)
- **Input / Ceiling / Output** in dB, geglättet (keine Klicks)
- **Mix** (Dry/Wet) und **Delta** (nur das abgeschnittene Signal hören)
- Mono- und Stereo-Bus-Layouts
- Alle Parameter über APVTS automatisiert und im Host speicherbar

## Parameter

| Parameter | Bereich | Standard | Bedeutung |
|---|---|---|---|
| Input | -24 … +24 dB | 0 dB | Eingangsverstärkung vor dem Clipper |
| Ceiling | -24 … 0 dB | -0,3 dB | Abschnittschwelle (Signal wird darauf normiert) |
| Shape | Hard / Soft / Cubic / Sine | Soft | Kennlinie |
| Knee (Soft) | 1 … 100 % | 40 % | Weichheit des Soft-Knie |
| Oversampling | Off / 2x / 4x / 8x / 16x | 4x | Oversampling vor dem Clipping |
| Mix | 0 … 100 % | 100 % | Anteil des geclippten Signals |
| Output | -24 … +12 dB | 0 dB | Ausgangspegel nach dem Ceiling |
| Delta | an/aus | aus | Nur Wet − Dry ausgeben |

## Windows-VST3 bauen

### Lokal (Windows, Visual Studio 2022)

Voraussetzungen: Visual Studio 2022 (C++-Desktop-Workload), CMake ≥ 3.22, Git.

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

Das fertige Plugin liegt danach unter:

```
build/HardEdgeClipper_artefacts/Release/VST3/HardEdgeClipper.vst3
```

### Installation im DAW

`HardEdgeClipper.vst3` in einen der VST3-Suchpfade des Hosts kopieren, z. B.:

```
C:\Program Files\Common Files\VST3\
```

### Per CI (GitHub Actions)

Bei jedem Push auf `main` (oder manuell über *Actions → Build Windows VST3 → Run workflow*) baut der Workflow [`.github/workflows/build-windows.yml`](.github/workflows/build-windows.yml) das VST3 auf einem `windows-2022`-Runner und stellt es als Artifact **HardEdgeClipper-VST3-Windows** zum Download bereit.

## Projektstruktur

```
CMakeLists.txt                      Projekt-Definition (JUCE via FetchContent)
Plugin.cpp                          Prozessor (DSP) + Editor
.github/workflows/build-windows.yml Windows-CI für das VST3-Artefakt
```
