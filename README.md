# Femto
Fast &amp; Lightweight, Windows-only, ASCII TUI, Micro-Kernel Architecture Software, with window tiling system and secure Plugin API, connecting plugin developers and users via Supabase.

Plugins need to be downloaded and enabled in order to be used. Plugins need to be with the pluginAPI header file. Plugin Devs can test their plugins locally by compiling a dll. And they can upload their plugins to the cloud by compressing their plugin to a zip and uploading that. Uploading a plugin sends a gmail to the admin, to review and approve/reject plugins only after which it would be available to users.

Heads-up: Press T for in-app tutorial.

Third Party Libraries:
    the javascript ones you can see in the package.
    in c++ i used nlohmann/json.hpp, by just downloading the files from GitHub and placing them in the femto\client stuff, and <cpr/cpr.h> which is upto you how to setup, i just installed it from msys2.
    keeping in mind that im using the g++ compiler, this was my terminal command to build the c++ files: 
        g++ main.cpp client.cpp -o main.exe -lole32 -lshell32 -loleaut32 -luuid -lcpr -lcurl -lssl -lcrypto

.env Stuff:
    I set PORT to 3000, and SERVER_BASE_URL to http://localhost:3000 (note in order to put this up on for example. render.com you would have to change the url here and in client.cpp.
    I entered my SUPABASE_URL and SUPABASE_KEY.
    I set GMAIL_USER to my own gmail, to test/approve/reject plugins as admin
    I set GMAIL_APP_PASSWORD to the App Password I got generated from google (do not use your regular password)
    
Supabase details:
    I created a storage bucket plugin-files
    And ran this sql in sql editor:
    
        -- Create Developers Table
        CREATE TABLE developers (
            id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
            username TEXT UNIQUE NOT NULL,
            api_key TEXT UNIQUE NOT NULL,
            created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
        );

        -- Create Plugins Table Linked to Developers
        CREATE TABLE plugins (
            id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
            name TEXT UNIQUE NOT NULL,
            creator TEXT NOT NULL,
            developer_id UUID REFERENCES developers(id) ON DELETE CASCADE,
            description TEXT,
            state TEXT DEFAULT 'pending', -- 'pending', 'approved', 'rejected'
            source_zip_url TEXT,
            dll_url TEXT,
            created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
        );

And lastly ofcourse, as long as youre running on local host, to use the features that interact with the cloud, youll need to run node server.js and .\main.exe, at the same time.


Limitations, ie. this includes things i am too tired to resolve:
 .  No advanced textbox-like component for plugin developers.
 .  No feedback to the developer, if their plugin has been rejected or still yet to be checked.
 .  No option for the developer to see a list of their own plugins, and neither an option to delete a plugin from the cloud.
 .  A bug, where a mouse click may translate to Key-presses in my code, so that a left-click would be translated as "1 is pressed" (as in the digit 1).
 .  Ofcourse, the risk that malicious zip files might harm the admins device upon attempting to review code inside.


Might come around for performance enhancements or removing those limitations or bug fixes (if any).


Given that enough plugins are made and you dont feel the absence the of acolors or a mouse, you might never need to ait the application
