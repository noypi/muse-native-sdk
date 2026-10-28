import { View, Text } from '@muse/native';
import { state } from '@muse/core';

// Theme tokens for light/dark theming.
export const theme = {
  light: { background: '#ffffff', foreground: '#111111' },
  dark: { background: '#111111', foreground: '#ffffff' },
};

const routes = ['home', 'about'] as const;
type Route = (typeof routes)[number];

// Minimal navigation state for the template app.
export function useNavigation(initial: Route = 'home') {
  const current = state<Route>(initial);
  function navigate(to: Route) {
    current.value = to;
  }
  return { current, navigate, routes };
}

async function loadMessage(): Promise<string> {
  const res = await fetch('https://example.com/api/hello');
  const data = (await res.json()) as { message?: string };
  return data.message ?? 'Hello, muse-native!';
}

export default function App() {
  const { current, navigate } = useNavigation();
  const message = state('Hello, muse-native!');
  void loadMessage().then(
    (text) => {
      message.value = text;
    },
    () => {},
  );
  return (
    <View theme={theme.light}>
      <Text>{message.value}</Text>
      <Text>{current.value}</Text>
      <View navigation="tabs">
        <Text onPress={() => navigate('home')}>Home</Text>
        <Text onPress={() => navigate('about')}>About</Text>
      </View>
    </View>
  );
}
