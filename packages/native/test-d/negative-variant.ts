// T4 negative type fixture — the bug entry's example value must NOT compile.
// `tsc --noEmit -p packages/native/tsconfig.json` passes with both fixtures
// because the error below is consumed by the expect-error directive; deleting the
// guard line (or widening the union to admit 'giant') makes the
// gate fail. SPEC-M3-NATIVE FR-M3-002 / AC-M3-004 (reject half).
import type { ButtonProps } from '../index';

// @ts-expect-error - 'giant' is not a valid Button variant (AC-M3-003 union)
const buttonGiant: ButtonProps = { variant: 'giant', label: 'x' };

export { buttonGiant };
