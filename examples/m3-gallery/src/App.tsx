// Throwaway fixture owned by run fr-tsx-008; real gallery is FR-SAMPLE-001/FR-M3-007; do not extend.
import { View, Text } from '@muse/native';
import { state } from '@muse/core';
import { ButtonScreen } from "./screens/ButtonScreen";
export default function App() {
  const title = state("hi");
  return (<View><Text>{title.value}</Text><ButtonScreen /></View>);
}
