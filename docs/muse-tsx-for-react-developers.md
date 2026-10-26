# Muse TSX for React Developers — Syntax Differences

> Audience: React / React Native developers coming to MuseNative.
> Spec sources: `manual-context.local/specs-tsx-dialect.md` (v1.8),
> `manual-context.local/specs-muse-core.md`, and the TSX entries
> (hostility #1, #7, #8) in `manual-context.local/author-hostile-rules.md`.
> Canonical package names are `@muse/*` (`muse` CLI, `muse.config.ts`,
> `*.mui.zip`); `capo` spellings resolve as deprecated aliases
> (RULE-TSX-005).

MuseNative components *look* like React function components, but they
compile **ahead of time** to a static UI tree (RULE-TSX-002) and run
**once per instance** with fine-grained signals — the Solid model, not
re-render (RULE-MCORE-001). There is no runtime JSX evaluator, no diff,
no hooks. Anything the AOT compiler cannot lower to a fixed C++ node
shape is a **build error with file/line/column**, reported alongside
`tsc` output in one build log (`tsc --noEmit` with
`jsxImportSource: "@muse/jsx"` + AOT legality checker, RULE-TSX-003).

Think of it this way: **if it would need JavaScript to run on every
frame or every render, it does not belong in the JSX slot — put it in
`computed()` / `state` from `@muse/core` first.**

---

## 1. At-a-glance cheat sheet

| # | Area | React you know | Muse TSX you write | Breaks build? |
|---|---|---|---|---|
| 1 | File layout | Many components per file, any filename | **One component per `PascalCase.tsx` file, default export only** (FR-TSX-001) | ✅ Yes — `1 component per .tsx in a PascalCase file, got <actual>` / `move <SecondName> to <SecondName>.tsx` |
| 2 | Mental model | Body re-runs on every render; `useState`/`useEffect`/`useMemo` | **Body runs once at setup**; `state`/`computed`/`effect`/`watch` from `@muse/core`; **no React hooks** (FR-MCORE-001) | ✅ Yes — hook imports fail type-check + bundle |
| 3 | Conditionals | `{count && <Badge/>}`, `{x ? <A/> : <B/>}`, early returns anywhere, `if` statements in JSX | **Ternary**, **`&&` with boolean-only left side**, **guarded early return** (`if (c) return <X/>` before one final return), or **`c-if` chain** (FR-TSX-004, FR-TSX-002) | ✅ Yes — non-boolean `&&`, misplaced/missing return fail |
| 4 | Directives | None (`{items.map(...)}` inline, ternaries everywhere) | Reserved **`c-if` / `c-else-if` / `c-else`, `c-for`, `c-show`**; **`c-` prefix reserved** — you cannot name your own component `c-*` (FR-TSX-002) | ✅ Yes — unknown `c-*`, non-adjacent `c-else`, custom `c-` name fail |
| 5 | Lists | Any `.map()` shape, `key` a warning | **One canonical shape**: `items.map((item) => <Row key={item.id} />)` — expression body, single JSX element, **`key` mandatory**; otherwise use **`c-for each key`** (FR-TSX-005) | ✅ Yes — block-bodied `.map`, missing `key` fail |
| 6 | Expressions | Assignments, `new`, `await`, arbitrary calls, dynamic `<Tag/>` | **Eval-free grammar only**: identifiers, member access, literals, ternary, `&&`/`\|\|`, comparisons, arithmetic, canonical `.map`, and **`/** @pure *\/` calls (lifted)**. Everything else — assignment, `new`, `await`, statements in slots, unannotated/side-effecting calls, dynamic tags — **fails** (FR-TSX-003, RULE-TSX-006) | ✅ Yes — diagnostic names the form + fix (`computed(() => …)` or add `/** @pure *\/`) |
| 7 | Derived values | `const x = format(v)` inline, `useMemo(() => …)` | `const x = computed(() => format(v))` then bind `{x.value}`; pure helpers get `/** @pure *\/` so the slot lifts to `__c_<hash>` (RULE-TSX-006) | Hoist or annotate, or build fails |
| 8 | Inputs | `value` + `onChange`, `defaultValue`, `{...props}` freely | **`value={bareWritableState}`** = two-way; **anything else needs `onChange`**; **`valueReadonly={expr}`** = one-way + requires `onChange`; **spread never creates two-way**; **no `model` prop** (FR-TSX-006) | ✅ Yes — derived/member/readonly `value` without `onChange`, missing `onChange`, `model` fail |
| 9 | Styling | `style={{ backgroundColor, marginTop }}`, any CSS string/number, `className` unchecked | **`style` camelCase only, numbers = dp**; **`class` resolved at build** against co-located CSS3/SASS — unknown class fails; literals allowed but theme-blind (FR-TSX-007, FR-TSX-010) | ✅ Yes (kebab keys, non-number/string values, unknown class); literal-duplicate-token = advisory `CAPO0012`, exit 0 |
| 10 | Animation | JS-driven (`Animated`, Reanimated worklets, CSS transitions inline) | **Declarative only**: `transition={{ opacity: "token" }}`, `spring(initial)` + `animateTo(target, { velocity })`, `sharedId`, named route patterns (FR-TSX-011) | ✅ Yes — literal curve/duration, unknown pattern fail; orphan `sharedId` warns |
| 11 | Imports / packages | Any npm package | **Four packages only**: `@muse/core` (reactivity), `@muse/native` (components + manifest), `@muse/jsx` (types only), `@muse/cli` (`muse init/build/dev/run`) (FR-TSX-008) | ✅ Yes — wrong-direction / unknown imports fail |
| 12 | Type errors | `tsc` *or* bundler *or* runtime | **One log, two validators**: `tsc` (props/types) + AOT (grammar); either fails the build, but both always run to completion (FR-TSX-009) | ✅ Yes — exit non-zero, validator named |

---

## 2. The three author-hostile TSX rules, translated

These are hostility #1, #7, #8 from `author-hostile-rules.md` — the
ones that turn working React habits into red builds.

### 2.1 Hostility #1 — Eval-free expression grammar (FR-TSX-003, RULE-TSX-002/006)

**React habit:** compute anything, anywhere:

```tsx
// React — fine
<Text>{(user.score = 5, new Intl.NumberFormat().format(user.score))}</Text>
<Text>{await fetchName()}</Text>
<Text>{format(user.name)}</Text>
const Tag = cond ? A : B; <Tag />;
```

**Muse TSX:** every one of those fails (AC-TSX-009/010). Allowed in a
`{…}` slot: identifiers, `a.b.c`, literals, `cond ? a : b`, `&&`/`||`,
comparisons, arithmetic, canonical `.map`, and pure-annotated calls.

```tsx
// Muse — hoist the work into reactivity first
import { computed } from "@muse/core";

/** @pure */
function formatName(name: string) { return name.trim().toUpperCase(); }

const label = computed(() => (ok.value ? formatName(user.name) : "—"));

export default function Greeting() {
  return <Text>{label.value}</Text>;   // slot is just a signal read
}
```

Rules of thumb:

- Unannotated call in JSX → build error that says *"put it in
  `computed(() => …)` and bind the result, or add `/** @pure *\/`"*.
- `/** @pure */` means: reads only its arguments, no I/O, no mutation,
  no `Date.now()` / `Math.random()`. The compiler lifts the **whole
  slot** into an instance-owned `computed` named `__c_<hash>` (visible
  in `--print-config`). When in doubt, write `computed` yourself —
  that is the recommended form for anything impure.
- `assignment`, `new`, `await`, statements, dynamic tag names: never
  legal in a slot. Move them above the return (setup runs once) or
  into `computed`/`effect`.

### 2.2 Hostility #7 — One component per `PascalCase.tsx` file (FR-TSX-001)

```tsx
// React — normal colocation: FAILS in Muse
export function Avatar() { … }
export function Badge() { … }
```

```tsx
// Muse — Avatar.tsx
export default function Avatar() { … }

// Muse — Badge.tsx
export default function Badge() { … }
```

- Basename must be PascalCase; importing from a non-PascalCase path
  has no defined resolution.
- Second component in the file → `1 component per .tsx, move
  <SecondName> to <SecondName>.tsx`. Helpers that are not components
  are fine; components are not.

### 2.3 Hostility #8 — `c-` built-ins, adjacency, `key`, boolean `&&` (FR-TSX-002/004/005)

| React | Muse | Why |
|---|---|---|
| `{count && <Badge/>}` (renders `0`) | `{count > 0 && <Badge/>}` — left side must be **boolean-typed** (AC-TSX-012 names the operand type) | Kills the stray-`0` bug at compile time |
| `{items.map(i => { …; return <Row/> })}` | `items.map((item) => <Row key={item.id} />)` — expression body, single element, `key` required; else `<c-for each={items} key="id">…</c-for>` | One lowerable keyed-list node |
| `{cond ? <A/> : null}` scattered | `<c-if test={cond}>…</c-if><c-else>…</c-else>` — `c-else-if`/`c-else` **only as adjacent siblings** of `c-if`/`c-else-if`; any element/text between breaks the chain | Static branch node |
| `<c-card/>` custom element | Forbidden — **`c-` prefix reserved** (AC-TSX-007) | Engine owns the namespace |
| `{show ? <X/> : null}` remount | `<c-show when={show}>…</c-show>` — toggles **visibility without removing** the subtree (Q-TSX-002 default: keep layout, hide paint) | Use when mount state must survive |

---

## 3. More React → Muse translations

### Conditionals (FR-TSX-004)

```tsx
// React
{isLoading ? <Spinner/> : <Content/>}
{user && <Menu/>}              // ← truthy object, works in React
function P() { if (a) return <A/>; doMore(); return <B/>; }
```

```tsx
// Muse — all three legal shapes
{isLoading ? <Spinner/> : <Content/>}
{user != null && <Menu/>}      // boolean expression on the left
function P() {
  if (isLoading) return <Spinner/>;  // guarded early return…
  return <Content/>;                 // …then exactly one final return
}
```

### Lists (FR-TSX-005)

```tsx
// React — tolerated
{items.map((item, i) => <Row key={i} data={item} />)}
{items.map(item => { const x = f(item); return <Row key={item.id} v={x}/> })}
```

```tsx
// Muse — canonical only, or c-for
{items.map((item) => <Row key={item.id} />)}
<c-for each={items} key="id"><Row /></c-for>
```

### Bindings (FR-TSX-006)

```tsx
// React
<input value={user.name} onChange={…} />
<input value={first.value + last.value} />   // renders, read-only-ish
<input {...props} />                          // may carry value
<MyInput model={x} />
```

```tsx
// Muse — intent visible at the call site
import { state } from "@muse/core";
const name = state("");
<TextInput value={name} />                        // bare writable → two-way
<TextInput valueReadonly={label.value} onChange={…} />  // one-way
<TextInput value={user.name} onChange={…} />      // member expr → onChange required
// spread never synthesizes two-way; `model` is rejected by tsc AND AOT
```

### Styling (FR-TSX-007, FR-TSX-010)

```tsx
// React
<div style={{ "background-color": "red", marginTop: 8 }} className="typo" />
```

```tsx
// Muse
<View style={{ backgroundColor: "#FF0000", marginTop: 8 }} class="typo" />
//         camelCase only ─┘  numbers are dp ─┘              └─ build-resolved;
//           kebab key or non-number/string value fails; unknown class fails
//           naming the class, component, and stylesheet searched.
```

Literals are legal in app code (unlike inside `@muse/native`), but a
literal that duplicates an M3 role-token value emits advisory
**`CAPO0012`** (exit 0) — and it will **not** follow light/dark or
Dynamic Color. Use a token reference for theme-aware styling.

### Animation (FR-TSX-011)

There is no per-frame JS. TypeScript declares targets; C++ evaluates.

```tsx
<Card transition={{ opacity: "m3-motion-fade", transform: "m3-motion-emphasized" }} />
const y = spring(0);
y.animateTo(120, { velocity: 300 });   // re-target keeps value + velocity
<Hero sharedId="cover" />
// route config: transition: "m3-shared-axis-x" — unknown names fail the build
```

### No hooks, no re-render (FR-MCORE-001)

| React | Muse (`@muse/core`) |
|---|---|
| `useState(init)` | `state(init)` → `.value` read/write |
| `useMemo(() => f(a))` | `computed(() => f(a))` |
| `useEffect(() => {…}, [a])` | `effect(() => {…})` / `watch(a, cb)` / `watchEffect` |
| `useRef` / `useCallback` | Unnecessary — body runs once, closures are stable |
| `useEffect` cleanup on update | `onUnmounted` (no `onUpdated` — nothing re-renders) |

## 4. Build errors you will meet first

1. `assignment / new / await / statement / unannotated call / dynamic
   tag` + *"put the call in `computed(() => …)`…"* → §2.1.
2. `1 component per .tsx…` → split the file (§2.2).
3. `&&` *names the operand type (number / possibly-undefined)* → make
   the left side boolean (§2.3).
4. `c-else*` *adjacency / missing `key` / unknown `c-*` / `c-` prefix* →
   fix the chain, add `key`, rename the component (§2.3).
5. `value … requires onChange` / unknown `model` → §Bindings.
6. `unknown class <x> in <Comp> (searched <css>)` / kebab-case key →
   §Styling.
7. `CAPO0012` advisory → literal duplicates a token; switch to the
   token if you want theming.

Both validators always run; the log names which one failed and where.
Fix the first error — AOT grammar errors usually mask nothing, but
`tsc` prop-type errors are cheaper to fix before restructuring slots.
