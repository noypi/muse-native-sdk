import { View, Text } from '@muse/native';

// Minimal flappy entry component (T4 scaffold).
// Gameplay (input/score/game-over loop) lands in T6; this keeps the
// target buildable so `./scripts/build-demos.sh --list` can name it.
export default function App() {
  return (
    <View>
      <Text>flappy</Text>
    </View>
  );
}
