# Tests that the command "weather in mountain view"
#   will return some data including the name of the town
#   to verify the Assistant SDK end-to-end.
set -e
set -x

./run_assistant --audio_input ./resources/weather_in_mountain_view.raw \
  --credentials_file ./credentials.json --credentials_type USER_ACCOUNT \
  | grep "Mountain View"
