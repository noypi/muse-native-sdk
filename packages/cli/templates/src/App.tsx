import { View, Text } from '@muse/native';
import { state } from '@muse/core';

export default function App() {
  const title = state('Hello, muse-native!');
  return (
    <View>
      <Text>{title.value}</Text>
    </View>
  );
}
