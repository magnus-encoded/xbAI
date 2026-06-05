# Run AI models on Xbox Series X

Hardware of XBSX is adequate to self host open models. Software is the blocker.
The software needs to run on a dev enabled console as a game. Console reached on the LAN (host/IP and dev account in your local `.env`); no auth required for the Device Portal at `https://<XBOX_HOST>:11443`.
Our end state goal will be a "game" application with access to full GPU capability that can run and serve an open weight model downloaded from huggingface in one button click.
User will start the xbox in dev mode, click the application on their controller, be offered the hugging face model, press a on the controller to select, it will download, launch and offer a dashboard showing the api endpoint, and display the usage.
