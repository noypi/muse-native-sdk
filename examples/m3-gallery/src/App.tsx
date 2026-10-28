import { View, Text } from '@muse/native';
import { ButtonScreen } from "./screens/ButtonScreen";
import { buildStories } from "./stories";

const manifest = require('../../../packages/native/muse.manifest.json');

export default function App() {
  const stories = buildStories(manifest);
  return (
    <View>
      <ButtonScreen />
      {stories.map((story, index) => {
        const rendered = story.render();
        return (
          <View key={`${story.component}-${story.variant}-${story.state}-${story.theme}-${index}`}>
            <Text>{`${story.component} ${story.variant} ${story.state} ${story.theme}`}</Text>
            <Text>{JSON.stringify(rendered)}</Text>
          </View>
        );
      })}
    </View>
  );
}
