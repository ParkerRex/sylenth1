# Visual design for the classic instrument

Apple guidance reviewed: 2026-10-13. Apply it alongside the Synthia project profile.

## Composition and spacing

Preserve the fixed classic canvas, section hierarchy, controls, and owned Synthia identity specified by `SPEC.md`. Apple does not prescribe a universal 8-point grid. Measure this product's geometry and alignment rather than importing a web spacing grid or responsive layout.

Inspect Part A/B and the affected LCD pages at each relevant supported scale. Check label/value clipping, menu placement, knob/fader alignment, selection, disabled states, and the correspondence between painted controls and hit regions.

## Typography

Use readable, consistent type and clear hierarchy within the classic surface. A custom typeface is possible; readability, units, and scaling matter more than replacing every label with San Francisco.

Apple's accessibility guidance lists **13 pt default and 10 pt minimum** for custom macOS type. Treat them as review guidance and report actual sizes and scales. Do not confuse a JUCE font-height value or bitmap pixel count with a macOS point measurement without checking the coordinate mapping.

Test the available enlargement paths and system magnification. Apple's HIG recommends allowing at least 200% text enlargement. A 2x PNG export alone does not prove the running editor offers that option or responds to macOS text settings. JUCE does not acquire Dynamic Type behavior just because it runs on macOS.

## Color and state

Keep state meanings consistent across parts and pages. Ensure selected, enabled, disabled, focused, and edited states remain understandable without color alone. Inspect text and symbols against their rendered backgrounds, including menus and overlays.

Test applicable macOS contrast and display settings. System colors can adapt automatically in native system controls; the custom JUCE surface needs actual evidence. Fixed classic colors are a product decision, not proof of accessible contrast. Report failures and scoped remedies without silently introducing a new skin or an unrequested light/dark system.

## Materials and motion

Apple's current materials guidance places Liquid Glass mainly in the navigation/control layer and recommends restraint with custom effects. These system-framework behaviors do not automatically apply to JUCE.

Synthia's classic panels are a fixed instrument surface. Preserve their owned appearance; do not add simulated glass, blur, new toolbars, or a macOS 27 dependency as part of applying HIG. Native system dialogs may follow the installed OS appearance independently of the instrument surface.

Use immediate state feedback and bounded meter/readout updates. Avoid decorative pulse, shimmer, blur, or spinner loops. Test meaningful feedback with Reduce Motion when relevant; no movement is required merely to make a control feel responsive.

## Official sources

- [Apple HIG: Typography](https://developer.apple.com/design/human-interface-guidelines/typography)
- [Apple HIG: Accessibility](https://developer.apple.com/design/human-interface-guidelines/accessibility)
- [Apple HIG: Color](https://developer.apple.com/design/human-interface-guidelines/color)
- [Apple HIG: Layout](https://developer.apple.com/design/human-interface-guidelines/layout)
- [Apple HIG: Materials](https://developer.apple.com/design/human-interface-guidelines/materials)
- [Apple HIG: Motion](https://developer.apple.com/design/human-interface-guidelines/motion)
