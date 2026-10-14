# Native accessibility review

Apple guidance reviewed: 2026-10-13. Review actual JUCE controls in the named host and Standalone separately.

## Names, roles, values, and state

Expose meaningful names, roles, current values, units, ranges/steps where relevant, and available actions. Identify independent controls with their musical context, such as oscillator A1, Filter B, or the shared filter control. Keep labels and values accurate after Part A/B and LCD-page changes.

Expose selected/checked/disabled state and meaningful group relationships. Decorative panel artwork should not crowd the accessibility tree. A control's title or tooltip alone does not prove that VoiceOver can inspect and operate it.

For custom components, inspect `createAccessibilityHandler`, the value/action interfaces, and state notifications in the pinned JUCE source. Accessibility edits must use the canonical parameter and normal host gestures; they must not create a second state model.

## Keyboard and assistive input

Exercise logical focus order, a visible focus indicator, activation, stepped/continuous adjustment, program text entry, context menus, Escape, and return of focus after dismissal. Verify that Part A/B or a page change does not leave focus on a hidden or wrongly bound control.

Test useful tasks with VoiceOver and Accessibility Inspector. Exercise Full Keyboard Access and other relevant input settings when available. Announce meaningful page/program changes and errors without announcing every meter update or stealing focus during playback.

DAWs can intercept keyboard input. Record what works in each tested format/host and distinguish host limitations from Synthia behavior. Static source and captures leave keyboard and spoken-output outcomes `untested`.

## Target dimensions

Apple's current Accessibility platform table specifies macOS controls at **28 x 28 pt default**, with **20 x 20 pt minimum**. The Buttons page separately offers broad **44 x 44 pt** hit-region advice across input methods. Use the macOS-specific table for the native pointer review, identify the source, and record when the broader guidance matters for a particular control or user need. Do not call 44 pt a universal Mac minimum.

Measure the interactive hit region and spacing, including menus, tiny toggles, program arrows, and keyboard/wheel controls. A smaller painted glyph can have a larger target. Enlargement must not introduce ambiguous overlapping targets.

Record JUCE logical bounds, effective editor scale, backing scale, and the resulting point dimensions. Pixel density alone does not increase a target's point size. If those mappings or hit regions are unknown, leave the measurement `untested`.

Report below-minimum targets as findings against Apple's guidance. Preserve `SPEC.md` as the product authority and describe any geometry conflict explicitly; the classic reference does not make a measured accessibility problem pass.

## Text and contrast

Review text enlargement, clipping, legibility, focus, and target alignment at supported scales. Test the actual running editor's enlargement path, not only a larger offscreen image.

Apple's Accessibility page uses WCAG AA contrast values in Accessibility Inspector: regular small text needs **4.5:1**; larger or bold text can use **3:1** under the cited classification. Use 4.5:1 for ordinary compact instrument labels. Record size, weight, foreground, effective background, method, and source before applying an exception.

The inherited skill limited contrast calculations to its web profile. Native contrast assessment is relevant here because Apple explicitly references it. A WCAG calculator's classification is its own standard; do not silently interchange that classification with Apple's table or claim whole-product compliance from a few sampled colors.

Measure real rendered states. Transparent, textured, and antialiased content needs representative sampling; a constant from source is not always the final foreground/background. Apple recommends a higher-contrast scheme under Increase Contrast when the default does not meet the cited minimum. Verify actual behavior rather than assuming custom JUCE controls adapt automatically.

## Display settings, feedback, and cognition

Check relevant Increase Contrast, Reduce Transparency, Reduce Motion, Differentiate Without Color, and magnification behavior. Color and sound must not be the sole indication of program state, successful save, failed load, or enabled effects.

Use consistent musical terms and keep current state inspectable. Distinguish selecting an effect page from enabling that effect. Keep errors readable long enough to understand and explain the next action without exposing unrelated paths or private data.

An instrument's generated notes are its content. Do not demand captions or transcripts for every synth note, add unrequested haptics, or silence host-owned MIDI to satisfy general media guidance. Operational feedback should have a visible and accessible equivalent; note ownership and playback safety follow `SPEC.md`.

## Evidence limits

Record the task, OS, host/format, input/settings, revision/binary, scale, expected behavior, observed result, and evidence. Native runtime operation is needed to claim success for reading order, focus movement, spoken values, assistive actions, and host gesture recording. Source inspection can identify a plausible defect, but cannot prove the entire task succeeds.

## Official sources

- [Apple HIG: Accessibility](https://developer.apple.com/design/human-interface-guidelines/accessibility)
- [Apple HIG: VoiceOver](https://developer.apple.com/design/human-interface-guidelines/voiceover)
- [Apple HIG: Buttons](https://developer.apple.com/design/human-interface-guidelines/buttons)
- [Apple HIG: Keyboards](https://developer.apple.com/design/human-interface-guidelines/keyboards)
- [Apple HIG: Typography](https://developer.apple.com/design/human-interface-guidelines/typography)
- [Apple HIG: Motion](https://developer.apple.com/design/human-interface-guidelines/motion)
