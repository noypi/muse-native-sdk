// T4 positive type fixture — every AC-M3-003-listed valid variant must compile.
// Included in packages/native/tsconfig.json; excluded from node --test.
// SPEC-M3-NATIVE FR-M3-002 / AC-M3-003 (valid variants), AC-M3-004 (accept half).
import type { ButtonProps, CardProps, ChipProps } from '../index';

const buttonFilled: ButtonProps = { variant: 'filled' };
const buttonTonal: ButtonProps = { variant: 'tonal' };
const buttonOutlined: ButtonProps = { variant: 'outlined' };
const buttonText: ButtonProps = { variant: 'text' };
const buttonElevated: ButtonProps = { variant: 'elevated' };

const cardElevated: CardProps = { variant: 'elevated' };
const cardFilled: CardProps = { variant: 'filled' };
const cardOutlined: CardProps = { variant: 'outlined' };

const chipAssist: ChipProps = { variant: 'assist' };
const chipFilter: ChipProps = { variant: 'filter' };
const chipInput: ChipProps = { variant: 'input' };
const chipSuggestion: ChipProps = { variant: 'suggestion' };

export {
  buttonFilled,
  buttonTonal,
  buttonOutlined,
  buttonText,
  buttonElevated,
  cardElevated,
  cardFilled,
  cardOutlined,
  chipAssist,
  chipFilter,
  chipInput,
  chipSuggestion,
};
