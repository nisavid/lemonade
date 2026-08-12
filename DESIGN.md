---
name: Lemonade
description: Calm, hardware-aware local AI controls with a restrained citrus identity.
colors:
  primary-citrus: "#FCD846"
  primary-citrus-hover: "#F0C040"
  light-canvas: "#FDFBF6"
  light-surface: "#F5F2EC"
  light-raised: "#FFFFFF"
  light-border: "#E2DDD1"
  light-ink: "#000000"
  light-muted: "#6B6B6B"
  dark-canvas: "#0A0A0A"
  dark-surface: "#1A1A1A"
  dark-inset: "#111111"
  dark-border: "#333333"
  dark-ink: "#FFFFFF"
  dark-muted: "#888888"
  focus-light: "#1976D2"
  focus-dark: "#75ABD7"
  status-success: "#4CAF50"
  status-warning: "#FFB74D"
  status-info: "#2196F3"
  status-error: "#F44336"
typography:
  title:
    fontFamily: "-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, sans-serif"
    fontSize: "1.25rem"
    fontWeight: 650
    lineHeight: 1.25
    letterSpacing: "normal"
  body:
    fontFamily: "-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, sans-serif"
    fontSize: "1rem"
    fontWeight: 400
    lineHeight: 1.5
    letterSpacing: "normal"
  label:
    fontFamily: "-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, sans-serif"
    fontSize: "0.75rem"
    fontWeight: 600
    lineHeight: 1.25
    letterSpacing: "normal"
  mono:
    fontFamily: "Consolas, Monaco, Courier New, monospace"
    fontSize: "0.8125rem"
    fontWeight: 400
    lineHeight: 1.45
    letterSpacing: "normal"
rounded:
  control: "4px"
  field: "6px"
  panel: "8px"
  card: "12px"
  pill: "9999px"
spacing:
  xs: "4px"
  sm: "8px"
  md: "12px"
  lg: "16px"
  xl: "24px"
  xxl: "32px"
components:
  button-primary:
    backgroundColor: "{colors.primary-citrus}"
    textColor: "{colors.light-ink}"
    typography: "{typography.label}"
    rounded: "{rounded.field}"
    padding: "8px 16px"
  button-primary-hover:
    backgroundColor: "{colors.primary-citrus-hover}"
    textColor: "{colors.light-ink}"
    typography: "{typography.label}"
    rounded: "{rounded.field}"
    padding: "8px 16px"
  field-light:
    backgroundColor: "{colors.light-raised}"
    textColor: "{colors.light-ink}"
    typography: "{typography.body}"
    rounded: "{rounded.field}"
    padding: "8px 12px"
  field-dark:
    backgroundColor: "{colors.dark-surface}"
    textColor: "{colors.dark-ink}"
    typography: "{typography.body}"
    rounded: "{rounded.field}"
    padding: "8px 12px"
  panel-light:
    backgroundColor: "{colors.light-surface}"
    textColor: "{colors.light-ink}"
    rounded: "{rounded.panel}"
    padding: "16px"
  panel-dark:
    backgroundColor: "{colors.dark-surface}"
    textColor: "{colors.dark-ink}"
    rounded: "{rounded.panel}"
    padding: "16px"
---

# Design System: Lemonade

## 1. Overview

**Creative North Star: "Crystalline Zest"**

Crystalline Zest means clear state, controlled energy, and a citrus accent that makes important actions easy to find. It does not mean decorative glass. Lemonade is a product tool, so the interface must disappear into model management, inference, telemetry, and recovery work.

The physical scene is a developer using Lemonade beside terminals and model workloads for a long session, sometimes in bright daylight and sometimes in a dim room. Both Light and Midnight modes must remain calm, dense, legible, and operationally trustworthy. The product uses a restrained color strategy: neutral surfaces carry the interface, while citrus occupies no more than roughly 10% of a working screen.

Lemonade must feel refreshing, pragmatic, and enabling. It must not resemble a cloud AI upsell, a vendor-locked hardware demo appliance, a GUI-only chat toy, a single-backend runner, an expert-only config maze, an enterprise control plane first, or a broad AI workspace that tries to replace partner applications.

**Key Characteristics:**

- Familiar desktop-tool structure with compact, consistent controls.
- Literal model, backend, server, and hardware state.
- Restrained citrus for primary actions and current selection.
- Tonal hierarchy before borders, shadows, blur, or decoration.
- Equal-quality Light and Midnight modes.
- Accessible feedback that never depends on color alone.

## 2. Colors

Neutral surfaces do the work. Citrus identifies the primary action or current selection. Semantic colors communicate health and risk only when paired with text, an icon, or a stable location.

### Primary

- **Citrus Action** (#FCD846): The `primary-citrus` token marks the strongest available action and current selection.
- **Pressed Citrus** (#F0C040): The `primary-citrus-hover` token is reserved for hover and active feedback on citrus controls.

### Secondary

- **Light Focus** (#1976D2): The `focus-light` token provides a contrast-safe focus boundary on Light surfaces.
- **Midnight Focus** (#75ABD7): The `focus-dark` token provides a contrast-safe focus boundary on Midnight surfaces.

### Tertiary

- **Success Green** (#4CAF50): The `status-success` token communicates a healthy or completed state.
- **Warning Amber** (#FFB74D): The `status-warning` token communicates degraded or attention-needed state.
- **Information Blue** (#2196F3): The `status-info` token communicates neutral operational information.
- **Error Red** (#F44336): The `status-error` token communicates failure and destructive consequences. Small text uses the theme's darker error-ink token; controls on this red use dark text.

### Neutral

- **Light Canvas** (#FDFBF6): The application background in bright environments.
- **Light Surface** (#F5F2EC): The primary pane and grouped-control surface.
- **Light Raised Surface** (#FFFFFF): Inputs and transient elements that need one tonal step of separation.
- **Light Structure** (#E2DDD1): Necessary dividers and control boundaries.
- **Light Ink** (#000000): Primary content on Light surfaces.
- **Light Muted Ink** (#6B6B6B): Secondary metadata that still meets readable contrast.
- **Midnight Canvas** (#0A0A0A): The application background in dim environments.
- **Midnight Surface** (#1A1A1A): The primary pane and grouped-control surface.
- **Midnight Inset** (#111111): Logs, code, and recessed operational regions.
- **Midnight Structure** (#333333): Necessary dividers and control boundaries.
- **Midnight Ink** (#FFFFFF): Primary content on Midnight surfaces.
- **Midnight Muted Ink** (#888888): Secondary metadata on Midnight surfaces.

**The Restrained Citrus Rule.** Citrus identifies actions and selection. It is never a decorative wash, page background, or generic status color.

**The State Has Words Rule.** Every actionable success, warning, information, error, pinned, loading, or pressure state has a text label or accessible name in addition to color.

**The Two Real Themes Rule.** Light and Midnight are complete working modes. Neither is a decorative inversion of the other.

## 3. Typography

**Body Font:** The native system sans stack.

**Label/Mono Font:** The same system sans for controls and a platform monospace stack for logs, commands, identifiers, and telemetry.

**Character:** Familiar, compact, and quiet. Typography creates hierarchy through fixed size and weight, not through display faces or fluid scale inside the application.

### Hierarchy

- **Title** (650, 1.25rem, 1.25): Page, panel, and dialog titles.
- **Body** (400, 1rem, 1.5): Explanations and conversational content, limited to 65 to 75 characters per line when prose is the task.
- **Compact body** (400, 0.8125rem to 0.875rem, 1.4): Model rows, settings, metadata, and dense operational content.
- **Label** (600, 0.75rem, 1.25): Short control labels and status keys. Sentence case is the default.
- **Mono** (400, 0.8125rem, 1.45): Logs, code, model identifiers, resource values, and commands.

**The Product Type Rule.** Application headings, labels, buttons, and data use fixed sizes from the product scale. Fluid display typography belongs only on distinct public brand surfaces.

**The Sentence Case Rule.** Uppercase is limited to established short telemetry abbreviations such as GPU, NPU, TPS, and TTFT. Buttons and prose use sentence case.

## 4. Elevation

Lemonade is tonal and flat by default. Canvas, pane, inset, and raised surfaces establish depth. A one-pixel divider is valid when it clarifies a resizable pane, menu, table boundary, or status bar. Shadows appear only when an element physically floats, such as a menu, popover, dialog, or toast.

### Shadow Vocabulary

- **Floating control** (`0 4px 8px rgba(0, 0, 0, 0.20)`): Menus, popovers, and toasts that must separate from content.
- **Dialog** (`0 8px 8px rgba(0, 0, 0, 0.28)`): Modal dialogs after inline and progressive alternatives have been exhausted.
- **Light Focus** (`0 0 0 2px #1976D2`): Contrast-safe keyboard focus around the control boundary on Light surfaces, never a decorative glow.
- **Midnight Focus** (`0 0 0 2px #75ABD7`): Contrast-safe keyboard focus around the control boundary on Midnight surfaces, never a decorative glow.

**The Tonal First Rule.** Use a surface change before adding a border, and use a border before adding a shadow.

**The No Ghost Card Rule.** Never combine a one-pixel border with a wide soft shadow. Resting panels use one structural treatment.

**The No Decorative Glass Rule.** Backdrop blur and translucent glass are not default product materials. They are allowed only when the background relationship conveys useful context and contrast remains verified.

## 5. Components

Components use familiar desktop affordances and expose complete default, hover, focus, active, disabled, loading, and error states where those states apply.

### Buttons

- **Shape:** Compact corners (`field`, 6px). Pills are reserved for tags and compact filters.
- **Primary:** Citrus surface, dark text, 8px by 16px padding, and no resting shadow.
- **Hover / Focus:** A darker citrus hover and the shared focus ring. Active feedback may shift by one pixel but must not choreograph movement.
- **Secondary:** Neutral surface with one quiet structural border. Destructive actions use explicit error language and color.
- **Labels:** Verb plus object, such as “Save settings,” “Load model,” and “Unpin model.”

### Chips

- **Style:** Compact pill only when the content is genuinely categorical, such as a model capability or active filter.
- **State:** Selected and unselected states differ through fill, label, and accessible state, not color alone.

### Cards / Containers

- **Corner Style:** Panels use `panel` (8px); occasional independent cards use `card` (12px).
- **Background:** Tonal surfaces are opaque enough for dense text and telemetry.
- **Shadow Strategy:** No shadow at rest. Floating surfaces use the vocabulary in Elevation.
- **Border:** Quiet structural borders are allowed for panes, lists, logs, status regions, and dialogs.
- **Internal Padding:** Dense product panels use 12px to 16px; spacious empty states may use 24px.

### Inputs / Fields

- **Style:** Opaque neutral field, one-pixel structural border, `field` radius, and a persistent visible label.
- **Focus:** Shared theme focus ring plus a border shift.
- **Error / Disabled:** Error text names the recovery action. Disabled fields remain readable and explain why they are unavailable when the reason is not obvious.

### Navigation

Navigation is stable, compact, and conventional. The current destination uses a tonal or citrus selection state. Icon-only controls require accessible names and visible tooltips. Mobile and narrow layouts collapse structure rather than shrinking typography.

### Model Residency Controls

Runtime state and durable preference are distinct. A model row can show “Loaded,” “In use,” “Pinned,” “Remembered pin,” “Starting,” “Rolling back,” “Cleanup required,” or a specific failure. The runtime pin action and the durable “Pin whenever loaded” preference must not look like the same control. A provisional startup member is shown as “Starting,” never “Loaded” or “Pinned,” until its complete compatible group commits. Rollback progress and cleanup-required states remain plan-linked and persistent. The global startup setting explains that conflicts stay remembered and unloaded; it does not redefine pinning itself.

Automatic pressure and admission actions must remain observable. When Lemonade clears an idle KV cache, evicts a model, refuses admission, or cannot relieve pressure because every candidate is pinned or in use, the UI and logs state what happened and why.

### Status and Feedback

The status bar is compact, persistent, and literal. Resource labels use tabular values and preserve distinct GPU, GTT/shared GPU memory, host memory, and NPU concepts where the platform exposes them. Toasts confirm short-lived actions; persistent failures remain beside the affected model or setting.

Motion lasts 150 to 250 milliseconds and communicates state only. Reduced-motion mode replaces spatial motion with an instant state change or a short crossfade. Loading content uses a stable skeleton or inline progress state rather than an isolated spinner in empty space.

## 6. Do's and Don'ts

### Do:

- **Do** keep the interface calm, exact, and trustworthy while preserving a small amount of citrus personality.
- **Do** prioritize the model, backend, server, and hardware state a user needs to make a decision.
- **Do** use familiar controls, keyboard access, visible focus, reduced motion, and readable status text.
- **Do** keep Light and Midnight modes equally complete and verify body text at 4.5:1 contrast or better.
- **Do** use one consistent component vocabulary across model management, settings, chat, logs, and telemetry.
- **Do** distinguish current residency, runtime pinning, durable pin preference, startup loading, and eviction eligibility in labels and help text.

### Don't:

- **Don't** make Lemonade feel like a cloud AI upsell or a vendor-locked hardware demo appliance.
- **Don't** make Lemonade feel like a GUI-only chat toy, a single-backend runner, or an expert-only config maze.
- **Don't** make Lemonade feel like an enterprise control plane first or a broad AI workspace that tries to replace Open WebUI, AnythingLLM, GAIA, or partner apps.
- **Don't** use decorative glassmorphism, gradient text, side-stripe borders, striped backgrounds, or hand-drawn fallback illustrations.
- **Don't** use identical card grids, nested cards, hero-metric templates, repeated uppercase eyebrows, or numbered-section scaffolding when order carries no meaning.
- **Don't** place a wide shadow on a bordered card or exceed a 16px radius on a product panel or card.
- **Don't** invent unusual scrollbars, dialogs, switches, or menus for brand flavor. Familiar behavior wins.
- **Don't** use decorative motion, orchestrated page-load sequences, or content-hidden reveal animations.
- **Don't** rely on color alone for connection, pressure, pin, loading, warning, or failure states.
- **Don't** allow labels, headings, or translated copy to overflow at narrow widths.
