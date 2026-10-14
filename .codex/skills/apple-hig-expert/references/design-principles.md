# Apple Design Principles

Apple reintroduced eight cross-platform design principles in June 2026. Use them to frame judgment, then apply the target platform and product profile for concrete requirements. They are decision tools, not a numeric compliance score.

Reviewed against the current Apple HIG on 2026-10-13. Apply the musical task and classic instrument contract when weighing the questions below.

## Audit Questions

### Purpose

Make something meaningful.

- Does this feature serve a clear user goal worth the time and attention it asks for?
- Is the most important outcome obvious before secondary information and controls?
- For Synthia: can a musician find and adjust the relevant oscillator, filter, modulation, arp, or effect control without leaving the classic surface?

### Agency

Let people act in their own way and recover from mistakes.

- Can people choose, pause, cancel, retry, undo, or revise an action where those controls matter?
- Does the interface clearly show what is happening, what the system needs, and what will happen next?
- For Synthia: can a musician inspect current values and recover from an unwanted program or parameter edit through the documented reset and program workflow?

### Responsibility

Act in people’s best interest.

- Does the product request only the data and permissions needed for the task?
- Are program replacement, file overwrite, invalid loads, and other consequential operations clear at the point of decision?
- Have plausible harmful outcomes been prevented or bounded instead of delegated to a warning alone?

### Familiarity

Build on what people already know.

- Do familiar controls use the platform’s expected placement, label, symbol, and behavior?
- Do controls that look alike behave alike across the product?
- Does the design preserve context and established product terms instead of inventing a new pattern for the same action?

### Flexibility

Adapt to diverse contexts and needs.

- Does the experience work across supported sizes, orientations, input methods, languages, abilities, and appearance settings?
- Can people personalize a setting when one fixed choice cannot serve the supported audience?
- Does each supported platform feel intentional while preserving the person’s context between platforms?

### Simplicity

Be clear and direct.

- Has every element earned its place by helping the current task?
- Can people tell what matters, what is interactive, and what happens next?
- Is the copy concise and concrete without hiding necessary context or actions?

### Craft

Care about every detail.

- Are typography, color, alignment, motion, audio, and state transitions coherent and precise?
- Does the interface respond promptly and remain stable during loading, resizing, scrolling, and failure?
- Has the team tested real content, accessibility settings, localization, and supported hardware rather than only the ideal state?

### Delight

Make it human.

- Does the experience create the intended feeling through usefulness, feedback, and care?
- Do moments of character support the task and product identity?
- Has decoration or motion begun to compete with clarity, comfort, or performance?

## Synthia emphasis

Prioritize precise musical editing, familiar classic controls, clear state, and dependable host behavior. Density is useful when related controls remain readable and operable. Simplicity does not justify deleting required sound controls or replacing the classic surface with a guided workflow.

- Keep names and units consistent across controls, touched-value displays, menus, host automation, and accessibility output. Distinguish part controls from shared controls.
- Check repeatable fine adjustment, reset, program selection, MIDI Learn/Forget, and clear error recovery without inventing a new undo or search system.
- Preserve live state and note ownership when parts, pages, programs, or editor lifetime change. A visually polished control still fails its task if it does not edit the real parameter.
- Let character come from useful feedback, owned instrument graphics, and dependable sound. Decorative motion competes with attention and plugin performance.

## Official Sources

- [Apple HIG: Design principles](https://developer.apple.com/design/human-interface-guidelines/design-principles)
- [Apple HIG: Writing](https://developer.apple.com/design/human-interface-guidelines/writing)
