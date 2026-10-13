# macOS and plugin-host guidance

Apple guidance reviewed: 2026-10-13. Apply this profile to Synthia's C++20/JUCE AU, VST3, and Standalone surfaces.

## Native Mac behavior

Apple's macOS guidance favors precise pointer input, keyboard access, sustained work, and useful information density. Apply those principles to sound editing: controls respond predictably, values remain readable, and related operations stay close to their context.

General app advice about resizable windows, menu bars, toolbars, and full screen needs a host boundary. A DAW owns its application menu, transport shortcuts, and plugin-window container. Review only behavior Synthia controls. Standalone window and command behavior can be assessed separately.

Synthia uses the fixed 908 x 591 logical classic canvas with uniform scaling. Preserve that composition. Test supported editor scales, display changes, and control alignment rather than prescribing responsive breakpoints or sidebar navigation.

Apple advises keeping critical controls away from a Mac window's bottom edge. Synthia's contract places its keyboard and performance controls there. Report any observed reachability problem and its conditions as a contract tradeoff; do not move the classic controls during an unrelated review.

## Keyboard and pointing input

- Check meaningful focus order, visible focus, activation, value adjustment, text entry, menu dismissal, and focus restoration.
- Test the actual AU/VST3 host. A key handler working in Standalone does not prove the DAW forwards that key. Keep host transport and system shortcuts usable when the editor has focus.
- Keep modifier-based fine adjustment, reset, and context-menu behavior consistent across equivalent controls. Distinguish parameter edits from onscreen note input.
- Check pointer capture and gesture completion when a drag leaves a control or the editor closes. Inspect mouse-wheel behavior for accidental edits while navigating.
- Do not add a shortcut, stepper, tooltip, or direct-value field only to satisfy a generic checklist. Recommend it when it addresses an observed task problem and fits the product contract.

## JUCE implementation boundaries

Use `src/plugin/PluginEditor.cpp` and `.h` for layout, interaction, focus, and custom accessibility handlers. Use `src/plugin/ParameterRegistry.*` for the canonical parameter's units, range, steps, choices, and display conversion. The processor owns host state, program operations, and bounded UI MIDI handoff.

JUCE custom controls do not automatically inherit AppKit or SwiftUI accessibility, Dynamic Type, semantic colors, or Liquid Glass. Verify the pinned JUCE version's actual APIs and behavior. Review keyboard and accessible edits against the same parameter and host gesture path used by pointer edits.

File dialogs and program/preset operations run outside audio processing. Changes to appearance or accessibility must preserve realtime boundaries, stable parameter IDs, note ownership, and self-contained host restore.

## Runtime evidence

Choose the cheapest reliable test for the affected behavior. Relevant tasks include fine-editing a parameter during playback, recording and playing its automation, switching Part A/B or an LCD page, opening and dismissing a menu, saving/restoring a program, and closing the editor with UI-owned notes active.

Record exact revision/binary, host and format, macOS version, preset/state, scale, sample rate/buffer, input/settings, steps, and result. Follow the broader repository host checks when a code change requires them. An audit of one task does not imply all host checks passed.

## Official sources

- [Apple HIG: Designing for macOS](https://developer.apple.com/design/human-interface-guidelines/designing-for-macos)
- [Apple HIG: Layout](https://developer.apple.com/design/human-interface-guidelines/layout)
- [Apple HIG: Keyboards](https://developer.apple.com/design/human-interface-guidelines/keyboards)
- [Apple HIG: Pointing devices](https://developer.apple.com/design/human-interface-guidelines/pointing-devices)
- [Apple HIG: Menus](https://developer.apple.com/design/human-interface-guidelines/menus)
- [Apple HIG: Sliders](https://developer.apple.com/design/human-interface-guidelines/sliders)
- [JUCE: AccessibilityHandler](https://docs.juce.com/master/classjuce_1_1AccessibilityHandler.html) (live reference; confirm APIs against the repository's pinned JUCE source)
