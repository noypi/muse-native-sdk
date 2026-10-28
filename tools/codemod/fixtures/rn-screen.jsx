import { View, Text, StyleSheet } from 'react-native';
import { createNativeStackNavigator } from '@react-navigation/native-stack';
import { CameraRig } from 'exotic-camera-sdk';

const Stack = createNativeStackNavigator();

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: '#ffffff' },
  title: { fontSize: 20, color: '#111111' },
});

function HomeScreen() {
  return (
    <View style={styles.container}>
      <Text style={styles.title}>Hello</Text>
    </View>
  );
}

function ScanScreen() {
  NativeModules.SecretBridge.startScan();
  return (
    <View style={styles.container}>
      <CameraRig mode="night" />
    </View>
  );
}

export default function App() {
  return (
    <Stack.Navigator>
      <Stack.Screen name="Home" component={HomeScreen} />
      <Stack.Screen name="Scan" component={ScanScreen} />
    </Stack.Navigator>
  );
}
