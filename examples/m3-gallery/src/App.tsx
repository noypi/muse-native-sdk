import { View, Text, manifest, TopAppBar, NavigationRail } from '@muse/native';
import { ButtonScreen } from "./screens/ButtonScreen";
import { buildStories } from "./stories";

export default function App() {
  const stories = buildStories(manifest);
  return (
    <View>
      <TopAppBar title="m3-gallery" />
      <NavigationRail selected="home" />
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
