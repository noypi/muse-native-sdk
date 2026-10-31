// Static M3 component showcase (FR-M3-007 / AC-M3-015 companion).
// FR-SAMPLE-014 T3 (AC-SAMPLE-041): the default gallery tree no longer uses
// this static root. Static Text-label rows were removed so the default tree
// contains zero static duplicates; the static fallback survives solely as the
// explicit-flag diagnostic (`--static-fallback` / `MUSE_STATIC_FALLBACK=1`,
// see src/gallerySurface.js `renderStaticFallback`), which reports the
// fallback and fails the proof gate. This file is kept for quarantine
// reference only and is not rendered on the default path.
// Fully static JSX: every component appears with literal props and literal
// text children, so the built tree carries the whole gallery with no JS
// evaluation on device. Covers all three tiers with their documented
// variants, each in a light row and a dark row. No hooks, no expressions,
// no imports beyond @muse/native except ./galleryMode for the
// static-fallback reporter.
// T61 state-row coverage (FR-M3-009 / AC-M3-020) is retained as
// manifest-generated story variants via buildStories in stories.js, not as
// hand-listed rows here (BUG-SAMPLE-003 T2).
import { View, Text, TopAppBar, NavigationBar, NavigationRail, NavigationDrawer, BottomAppBar, SearchBar, SegmentedButton, ButtonGroup, SplitButton, Button, IconButton, Fab, ExtendedFab, Card, Chip, Checkbox, Radio, Switch, Slider, LinearProgress, CircularProgress, Badge, ListItem, Avatar, Icon, Image, Divider, TextFieldEditCore, TextInput, Dialog, BottomSheet, Menu, SnackbarHost, Tooltip, Carousel, Surface, Scroll, Stack, Ripple, OverlayHost } from '@muse/native';
import { reportStaticFallback } from "./galleryMode";

// BUG-SAMPLE-003 T4 — this static Showcase renders only behind the explicit
// MUSE_GALLERY_STATIC_FALLBACK=1 flag (mode 'static-fallback'); the default
// 'generated' path renders only manifest-generated real tags. Rendering this
// static root explicitly reports the fallback (AC-RUNTIME-032).

export default function Showcase() {
  reportStaticFallback();
  return (
    <View>
      <View theme="light">
        <TopAppBar title="m3-gallery" size="small" />
        <TopAppBar title="m3-gallery" size="medium" />
        <TopAppBar title="m3-gallery" size="large" />
        <NavigationBar selected="home" />
        <NavigationRail selected="home" />
        <NavigationDrawer selected="home" />
        <BottomAppBar title="gallery" />
        <SearchBar value="" placeholder="Search settings" />
        <SegmentedButton selected="day" />
        <ButtonGroup selected="bold" />
        <SplitButton label="Create new" />
        <Button variant="filled" label="Primary action" />
        <Button variant="tonal" label="Tonal action" />
        <Button variant="outlined" label="Outlined action" />
        <Button variant="text" label="Text action" />
        <Button variant="elevated" label="Elevated action" />
        <IconButton variant="filled" icon="favorite" />
        <Fab size="medium" icon="add" />
        <ExtendedFab label="Compose" icon="edit" />
        <Card variant="elevated" />
        <Card variant="filled" />
        <Card variant="outlined" />
        <Chip variant="assist" label="Assist chip" />
        <Chip variant="filter" label="Filter chip" />
        <Chip variant="input" label="Input chip" />
        <Chip variant="suggestion" label="Suggestion chip" />
        <Checkbox checked={true} enabled={true} />
        <Checkbox checked={false} enabled={true} />
        <Radio selected={true} enabled={true} />
        <Radio selected={false} enabled={true} />
        <Switch checked={true} enabled={true} />
        <Switch checked={false} enabled={true} />
        <Slider value={0.6} enabled={true} />
        <LinearProgress progress={0.6} />
        <CircularProgress progress={0.4} />
        <Badge label="3" />
        <ListItem headline="Inbox headline" supporting="Three new messages arrived" />
        <Avatar label="AB" size={40} />
        <Icon name="favorite" size={24} />
        <Image source="gallery-hero" contentFit="cover" />
        <Divider orientation="horizontal" /><TextFieldEditCore value="" placeholder="Search settings" enabled={true} /><TextInput value="" placeholder="Search settings" />
        <Dialog open={true} title="Confirm delete dialog" />
        <BottomSheet open={true} />
        <Menu open={true} />
        <SnackbarHost message="Saved to library snackbar" />
        <Tooltip text="Favorite tooltip" />
        <Carousel index={1} />
        <Surface tone="tonal" elevation={1} />
        <Scroll orientation="vertical" />
        <Stack direction="column" spacing={8} />
        <Ripple bounded={true} />
        <OverlayHost />
      </View>
      <View theme="dark">
        <TopAppBar title="m3-gallery" size="small" />
        <TopAppBar title="m3-gallery" size="medium" />
        <TopAppBar title="m3-gallery" size="large" />
        <NavigationBar selected="home" />
        <NavigationRail selected="home" />
        <NavigationDrawer selected="home" />
        <BottomAppBar title="gallery" />
        <SearchBar value="" placeholder="Search settings" />
        <SegmentedButton selected="day" />
        <ButtonGroup selected="bold" />
        <SplitButton label="Create new" />
        <Button variant="filled" label="Primary action" />
        <Button variant="tonal" label="Tonal action" />
        <Button variant="outlined" label="Outlined action" />
        <Button variant="text" label="Text action" />
        <Button variant="elevated" label="Elevated action" />
        <IconButton variant="filled" icon="favorite" />
        <Fab size="medium" icon="add" />
        <ExtendedFab label="Compose" icon="edit" />
        <Card variant="elevated" />
        <Card variant="filled" />
        <Card variant="outlined" />
        <Chip variant="assist" label="Assist chip" />
        <Chip variant="filter" label="Filter chip" />
        <Chip variant="input" label="Input chip" />
        <Chip variant="suggestion" label="Suggestion chip" />
        <Checkbox checked={true} enabled={true} />
        <Checkbox checked={false} enabled={true} />
        <Radio selected={true} enabled={true} />
        <Radio selected={false} enabled={true} />
        <Switch checked={true} enabled={true} />
        <Switch checked={false} enabled={true} />
        <Slider value={0.6} enabled={true} />
        <LinearProgress progress={0.6} />
        <CircularProgress progress={0.4} />
        <Badge label="3" />
        <ListItem headline="Inbox headline" supporting="Three new messages arrived" />
        <Avatar label="AB" size={40} />
        <Icon name="favorite" size={24} />
        <Image source="gallery-hero" contentFit="cover" />
        <Divider orientation="horizontal" /><TextFieldEditCore value="" placeholder="Search settings" enabled={true} /><TextInput value="" placeholder="Search settings" />
        <Dialog open={true} title="Confirm delete dialog" />
        <BottomSheet open={true} />
        <Menu open={true} />
        <SnackbarHost message="Saved to library snackbar" />
        <Tooltip text="Favorite tooltip" />
        <Carousel index={1} />
        <Surface tone="tonal" elevation={1} />
        <Scroll orientation="vertical" />
        <Stack direction="column" spacing={8} />
        <Ripple bounded={true} />
        <OverlayHost />
      </View>
    </View>
  );
}

// BUG-M3-FIDELITY-008 L1 — layering page (FR-M3-010 / AC-M3-022 / RULE-M3-006).
// BUG-M3-FIDELITY-003 T2 — live probe text page (FR-M3-006, single-probe
// scope BQ1, API-29-only gate). The device presenter replaces this page's
// static TextInput box with the live evaluating field once
// AttachLiveTextField("m3-text-probe", deps) succeeds (RULE-M3-004 engines:
// FR-TEXT-008 InputConnection bridge, FR-TEXT-009 editing engine, FR-TEXT-013
// clipboard transport, plus the live FR-LAYOUT-012 imeBottom feed reusing
// the anchorSheetToImeInset per-frame pattern). Static pages above are
// untouched; the static-box painter stays as the fail-closed fallback.
export function TextProbePage() {
  return (
    <View sharedId="m3-text-probe">
      <Text>Text probe page</Text>
      <TextInput value="" placeholder="Probe live field" insetSource="FR-LAYOUT-012" />
    </View>
  );
}
// Static scene: dimmed content background plus all five overlays stacked on
// exactly one OverlayHost in submission order, dimming layer submitted before
// overlay content. The dismiss hook site is declared on the host below; the
// dismiss behavior itself is owned by L3.
export function LayeringPage() {
  reportStaticFallback();
  return (
    <View sharedId="layering-page">
      <Text>Layering page</Text>
      <View theme="light" dimmed={true}>
        <Text>Layering content dimmed light</Text>
      </View>
      <View theme="dark" dimmed={true}>
        <Text>Layering content dimmed dark</Text>
      </View>
      <TextInput value="" placeholder="Layering IME probe" insetSource="FR-LAYOUT-012" />
      <OverlayHost onScrimTap="dismiss-topmost">
        <Surface tone="scrim" />
        <Dialog open={true} title="Layering dialog" />
        <BottomSheet open={true} imeSafe={true as const} insetSource="FR-LAYOUT-012" />
        <Menu open={true} />
        <SnackbarHost message="Layering snackbar" />
        <Tooltip text="Layering tooltip" />
      </OverlayHost>
    </View>
  );
}
