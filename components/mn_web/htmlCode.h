// This file is part of MiNOS (MiNuS OS).
// Copyright (c) 2025 TheMiNuS
// Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
// See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
//
const char HTML_ROOT[] =  R"(
    <!DOCTYPE html>
    <html>
        <head>
            <title>The MiNuS OS</title>
            <meta name='viewport' content='width=device-width, initial-scale=1'>
            <meta http-equiv='refresh' content='5;url=/'>
            <link rel="stylesheet" href="styles.css">
        </head>
        <body>
            <h1>The MiNuS OS</h1>
            <div class='home-button-container'>
                <a class='button' href='/module-configuration'>Module Configuration</a>
                <a class='button' href='/example'>Examples</a>
                <fieldset>
                    <legend>Status</legend>
                    <div class='form-group'>
                        <p> %CurrentDate% - %CurrentTime% </p>
                        <p> %heartBeat% </p>
                    </div>
                </fieldset>
            </div>
            %COPYRIGHT%
        </body>
    </html>
)";

const char HTML_MODULE_CONFIGURATION[] =  R"(
  <!DOCTYPE html>
  <html>
    <head>
        <meta charset='UTF-8'>
        <meta name='viewport' content='width=device-width, initial-scale=1'>
        <title>ESP Configuration page</title>
        <meta http-equiv='refresh' content='120;url=/'>
        <link rel="stylesheet" href="styles.css">
    </head>
    <body>
        <h1>ESP Configuration Page</h1>
        <div id='message'>
            <p></p>
        </div>

        <form method='get' action='/wifi' class='config-form'>
          <fieldset>
              <legend>Wifi Configuration</legend>
              <div class='form-group'>
                  <label for='wifiSSID'>SSID:</label>
                  <input type='text' id='wifiSSID' name='wifiSSID' value='%wifi_ssid%'>
              </div>
              <div class='form-group'>
                  <label for='wifiPassword'>Wifi Password:</label>
                  <input type='password' id='wifiPassword' name='wifiPassword' value='%wifi_password%'>
              </div>
          </fieldset>
          <fieldset>
              <legend>Web Interface Configuration</legend>
              <div class='form-group'>
                  <label for='httpLogin'>Login:</label>
                  <input type='text' id='httpLogin' name='httpLogin' value='%http_login%'>
              </div>
              <div class='form-group'>
                  <label for='httpPassword'>Password:</label>
                  <input type='password' id='httpPassword' name='httpPassword' value='%http_password%'>
              </div>
              <div class='form-group'>
                  <label for='hostname'>Hostname:</label>
                  <input type='text' id='hostname' name='hostname' value='%hostname%'>
              </div>
          </fieldset>
          <div class='config-button-container'>
              <a href='/' class='button'>Go Back</a>
              <input type='submit' class='button' value='Save'>
          </div>
        </form>

        <form class='config-form' onsubmit='return false;'>
            <fieldset>
                <legend>Firmware update</legend>
                <div class='form-group'>
                    <input id='update' type='file' accept='.bin,application/octet-stream'>
                </div>
                <div class='config-button-container'>
                    <button type='button' class='button' onclick='mn_do_update()'>Firmware Update</button>
                </div>
                <div class='form-group'>
                    <small id='update_status'>Select a .bin then click Firmware Update.</small>
                </div>
            </fieldset>
        </form>

        <script>
        async function mn_do_update() {
            const f = document.getElementById('update').files[0];
            const st = document.getElementById('update_status');
            if (!f) { st.textContent = 'No file selected.'; return; }

            st.textContent = 'Uploading... do not close this page.';

            try {
                const res = await fetch('/doUpdate', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/octet-stream' },
                    body: f
                });

                const txt = await res.text().catch(() => '');
                if (!res.ok) {
                    st.textContent = 'Upload failed: HTTP ' + res.status + ' ' + txt;
                    return;
                }
                st.textContent = 'Upload OK. Device is rebooting...';
            } catch (e) {
                st.textContent = 'Upload error: ' + (e && e.message ? e.message : e);
            }
        }
        </script>

        <form method='POST' action='/factory-reset' enctype='multipart/form-data' class='config-form'>
            <fieldset>
                <legend>Factory Reset</legend>
                <div class='config-button-container'>
                    <input type='submit' class='button' value='Factory Reset'>
                </div>
            </fieldset>
        </form>


      %COPYRIGHT%

    </body>
  </html>
)";

const char HTML_PUSH_CONFIGURATION_TO_MODULE[] =  R"(
  <!DOCTYPE html>
  <html>
  <head>
      <meta name='viewport' content='width=device-width, initial-scale=1'>
      <link rel="stylesheet" href="styles.css">
      <title>Push Configuration to Flash and Reboot</title>
  </head>
  <body>
      <div id="loading">
          <div class="spinner"></div>
          <p>Rebooting...</p>
      </div>
        <script>
            window.location.href = '/reboot';
            setTimeout(function () {
               window.location.href = '/';
           }, 1000);
      </script>
  </body>
  </html> 
)";

const char HTML_FIRMWARE_UPGRADE_ERROR[] =  R"(
  <!DOCTYPE html>
  <html>
  <head>
      <meta name='viewport' content='width=device-width, initial-scale=1'>
      <link rel="stylesheet" href="styles.css">
      <title>Error during Firmware Upgrade</title>
  </head>
  <body>
      <div id="loading">
          <div class="spinner"></div>
          <p>Firmware upgrade error !</p>
      </div>
        <script>
            /* window.location.href = '/reboot'; */
            setTimeout(function () {
               window.location.href = '/';
           }, 3000);
      </script>
  </body>
  </html> 
)";

const char HTML_FIRMWARE_UPGRADE_SUCCESSFULL[] =  R"(
  <!DOCTYPE html>
  <html>
  <head>
      <meta name='viewport' content='width=device-width, initial-scale=1'>
      <link rel="stylesheet" href="styles.css">
      <title>Error during Firmware Upgrade</title>
  </head>
  <body>
      <div id="loading">
          <div class="spinner"></div>
          <p>Successfull upgrade: Rebooting ...</p>
      </div>
        <script>
            window.location.href = '/reboot';
            setTimeout(function () {
               window.location.href = '/';
           }, 1000);
      </script>
  </body>
  </html> 
)";

const char HTML_EXEMPLE[] =  R"(
 <!DOCTYPE html>
  <html>
    <head>
        <meta charset='UTF-8'>
        <meta name='viewport' content='width=device-width, initial-scale=1'>
        <meta http-equiv='refresh' content='5;url=/example'> <!-- Refresh every 5s -->
        <title>Examples</title>
        <link rel="stylesheet" href="styles.css">
    </head>
    <body>
        <h1>Examples</h1>

        <fieldset>
          <legend>Network</legend>
          <div class='form-group'>
            <p>MAC address: %MAC%</p>
            <p>IPv4 address: %IP_ADDR%</p>
            <p>Netmask: %NETMASK%</p>
            <p>Gateway: %GATEWAY%</p>
            <p>DNS: %DNS%</p>
          </div>
        </fieldset>

        <fieldset>
          <legend>GPIOs</legend>
          <div class='form-group'>
            <p>Digital (GPIO %EX_GPIO_D%): %GPIO_D_IN%</p>
            <p>Analog  (GPIO %EX_GPIO_A%): %GPIO_A_IN_mV% mV</p>
          </div>
        </fieldset>

        <div class='config-button-container'>
          <a class='button' href='/'>Go Back</a>
        </div>

        %COPYRIGHT%
    </body>
  </html>
)";

const char HTML_CSS_STYLE[] = R"(
    body {
        background-color: #080808; /* Dark background */
        color: #D4D4D4; /* Light text color for contrast */
        font-family: Arial, sans-serif;
        text-align: center;
        display: flex;
        flex-direction: column;
        justify-content: flex-start;
        align-items: center;
        min-height: 100vh;
        margin: 0;
        padding-bottom: 60px;
    }

    /* Apply dark blue mode styles to all elements */
    *, *::before, *::after {
        box-sizing: border-box;
        background-color: #080808; /* Dark blue background for most elements */
        color: #FFFFFF; /* Light text color for most elements */
        border-color: #252526; /* Dark blue border color for most elements */
    }

    .button {
        padding: 15px 30px;
        background-color: #333; /* Slightly darker blue for buttons */
        text-decoration: none;
        font-size: 18px;
        margin: 10px;
        border-radius: 5px;
        transition: background-color 0.3s ease;
    }

    .button:hover {
        background-color: #021e70; /* Darker blue on hover for buttons */
    }

    .config-form {
        width: 80%;
        max-width: 400px;
        margin: 0 auto;
    }

    fieldset {
        border-radius: 10px;
        padding: 10px;
        margin-bottom: 20px;
        /* border-color: #2D3B53; */
    }

    legend {
        font-weight: bold;
        text-align: center;
    }

    .form-group {
        text-align: left;
    }

    .form-group label {
        display: block;
        font-weight: bold;
        margin-bottom: 5px;
        margin-top: 10px;
    }

    /* Apply a different background color for input and select elements */
    .form-group input,
    .form-group select {
        width: 100%;
        padding: 10px;
        border-radius: 5px;
        font-size: 16px;
        background-color: #1E1E1E; /* Darker background for input and select */
        color: #D4D4D4; /* Text color for input and select */
    }

    .button-container {
        text-align: center;
    }

    .config-button-container {
        text-align: center;
    }

    .home-button-container {
        text-align: center;
    }

    /* Override the display for home page buttons */
    .home-button-container .button {
        display: block;
    }

    .config-details {
        width: 80%;
        max-width: 600px;
        margin: 0 auto;
        text-align: left;
    }

    .config-group {
        border-radius: 5px;
        padding: 15px;
        margin-bottom: 20px;
      /*  border-color: #2D3B53;*/
    }

    .config-group h2 {
        font-size: 24px;
        margin-top: 0;
    }

    .config-group p {
        margin: 5px 0;
    }

    .status-dot {
        width: 20px;
        height: 20px;
        border-radius: 50%;
        display: inline-block;
        margin-right: 5px;
    }

    .green-dot {
        background-color: #28a745; /* Green */
    }

    .yellow-dot {
        background-color: #ffc107; /* Yellow */
    }

    .orange-dot {
        background-color: #fd7e14; /* Orange */
    }

    .red-dot {
        background-color: #dc3545; /* Red */
    }

    .grey-dot {
        background-color: #6c757d; /* Grey */
    }

    .status-container {
        display: flex;
        align-items: center;
        margin-bottom: 10px;
    }

    .status-text {
        margin-left: 10px;
    }

    footer {
        position: fixed;
        bottom: 0;
        width: 100%;
        background-color: #1c1c1c;
        padding: 10px 0;
    }

    footer p {
        margin: 0;
        /* color: #FFFFFF; */
        background-color: #1c1c1c;
        font-size: 14px;
    } 

    #loading {
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        height: 66vh;
    }

    #loading p {
        margin-bottom: 10px;
    }

    .spinner {
        width: 40px;
        height: 40px;
        border-radius: 50%;  /* With 25% you have a funny square */
        border: 4px solid #333;
        border-top: 4px solid transparent;
        animation: spin 2s linear infinite;
    }

    @keyframes spin {
        0% { transform: rotate(0deg); }
        100% { transform: rotate(360deg); }
    }

)";
