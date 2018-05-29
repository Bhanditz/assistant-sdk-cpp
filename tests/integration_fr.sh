# Tests that the command "how do you say hello in spanish"
#   will return "buenos dias" to verify the Assistant SDK
#   end-to-end.
set -e
set -x

echo "comment dit-on bonjour en español" | ./run_assistant --text_input \
  --credentials_file ./credentials.json --credentials_type USER_ACCOUNT \
  --locale "fr-FR" --verbose | grep "Buenos dias"
