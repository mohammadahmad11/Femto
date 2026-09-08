require('dotenv').config();
const express = require('express');
const cors = require('cors');
const multer = require('multer');
const { createClient } = require('@supabase/supabase-js');
const nodemailer = require('nodemailer');
const fs = require('fs');
const path = require('path');
const { exec } = require('child_process');
const unzipper = require('unzipper');
const crypto = require('crypto');
const AdmZip = require('adm-zip');

const app = express();
app.use(cors());
app.use(express.json());

// Supabase Setup
const supabase = createClient(process.env.SUPABASE_URL, process.env.SUPABASE_KEY);

// File Upload Config
const upload = multer({ dest: 'temp_uploads/' });

// Email Transporter Config
const transporter = nodemailer.createTransport({
  service: 'gmail',
  auth: {
    user: process.env.GMAIL_USER,
    pass: process.env.GMAIL_APP_PASSWORD,
  },
});

/* ==========================================================================
   1. DEVELOPER REGISTRATION
   ========================================================================== */
app.post('/api/developers/register', async (req, res) => {
  try {
    const { username } = req.body;
    if (!username) return res.status(400).json({ error: 'Username is required.' });

    // Generate unique random API key
    const apiKey = 'dev_key_' + crypto.randomBytes(16).toString('hex');

    const { data, error } = await supabase
      .from('developers')
      .insert([{ username, api_key: apiKey }])
      .select()
      .single();

    if (error) {
      if (error.code === '23505') return res.status(400).json({ error: 'Username is already taken.' });
      console.log(error)
      throw error;
    }

    res.json({
      message: 'Registration successful! Store your API key safely.',
      username: data.username,
      apiKey: apiKey
    });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

/* ==========================================================================
   2. DEVELOPER PLUGIN UPLOAD
   ========================================================================== */
app.post('/api/plugins/upload', upload.single('source'), async (req, res) => {
  const file = req.file;

  try {
    const { name, description, apiKey } = req.body;

    if (!apiKey) {
      if (file) fs.unlinkSync(file.path);
      return res.status(401).json({ error: 'API key required.' });
    }

    if (!file) {
      return res.status(400).json({ error: 'Source zip file required.' });
    }

    // Verify Developer via API Key
    const { data: dev, error: devErr } = await supabase
      .from('developers')
      .select('id, username')
      .eq('api_key', apiKey)
      .single();

    if (devErr || !dev) {
      fs.unlinkSync(file.path);
      return res.status(403).json({ error: 'Invalid API Key.' });
    }

    // Check Duplicate Plugin Name
    const { data: existingPlugin } = await supabase
      .from('plugins')
      .select('id')
      .ilike('name', name)
      .maybeSingle();

    if (existingPlugin) {
      fs.unlinkSync(file.path);
      return res.status(400).json({ error: `A plugin named "${name}" already exists.` });
    }

    // Upload ZIP to Supabase Storage Bucket ('plugin-files')
    const fileBytes = fs.readFileSync(file.path);
    const storagePath = `sources/${Date.now()}_${file.originalname}`;

    const { error: storageErr } = await supabase.storage
      .from('plugin-files')
      .upload(storagePath, fileBytes, { contentType: 'application/zip' });

    if (storageErr) throw storageErr;


    // Insert Metadata linked to Developer ID
    const { data: dbData, error: dbErr } = await supabase
      .from('plugins')
      .insert([{
        name,
        creator: dev.username,
        developer_id: dev.id,
        description,
        state: 'pending',
        source_zip_url: storagePath
      }])
      .select()
      .single();

    if (dbErr) throw dbErr;

    // Cleanup local temp file
    fs.unlinkSync(file.path);

    // Send Notification Email
    const approveUrl = `${process.env.SERVER_BASE_URL}/api/admin/review/${dbData.id}?action=approve`;
    const rejectUrl = `${process.env.SERVER_BASE_URL}/api/admin/review/${dbData.id}?action=reject`;
    const downloadSourceUrl = `${process.env.SERVER_BASE_URL}/api/plugins/download-source?path=${encodeURIComponent(storagePath)}`;

    await transporter.sendMail({
      from: process.env.GMAIL_USER,
      to: process.env.GMAIL_USER,
      subject: `[Plugin Submission] ${name} by ${dev.username}`,
      html: `
        <h3>New Plugin Submitted</h3>
        <p><b>Name:</b> ${name}</p>
        <p><b>Creator:</b> ${dev.username}</p>
        <p><b>Description:</b> ${description}</p>
        <p><a href="${downloadSourceUrl}">Download Source ZIP to Inspect</a></p>
        <br/>
        <a href="${approveUrl}" style="background:green;color:white;padding:10px 15px;text-decoration:none;">APPROVE & COMPILE .DLL</a>
        &nbsp;&nbsp;
        <a href="${rejectUrl}" style="background:red;color:white;padding:10px 15px;text-decoration:none;">REJECT & DELETE</a>
      `,
    });

    res.json({ message: 'Plugin uploaded successfully and pending approval.' });
  } catch (err) {
    if (file && fs.existsSync(file.path)) fs.unlinkSync(file.path);
    res.status(500).json({ error: err.message });
  }
});

/* ==========================================================================
   3. ADMIN REVIEW (Approve & Compile DLL / Reject)
   ========================================================================== */
app.get('/api/admin/review/:id', async (req, res) => {
  const { id } = req.params;
  const { action } = req.query;

  try {
    const { data: plugin, error: fetchErr } = await supabase
      .from('plugins')
      .select('*')
      .eq('id', id)
      .single();
    
    if (fetchErr || !plugin) return res.status(404).send('Plugin not found.');

    if (action === 'reject') {
      
      await supabase.from('plugins').delete().eq('id', id);
      const { data, error2 } = await supabase
        .storage
        .from('plugin-files') // e.g., 'plugin-files'
        .remove([plugin.source_zip_url]); // Pass an array of file paths to delete
      if (error2) {
        throw new Error(`Error deleting file: ${error.message}`)
      }
      return res.send('<h1>Plugin Rejected & Removed</h1>');
    }

    if (action === 'approve') {
      const workDir = path.join(__dirname, `build_${id}`);
      if (!fs.existsSync(workDir)) fs.mkdirSync(workDir);
      
      // 1. Download directly using the Supabase client
      const { data, error } = await supabase
        .storage
        .from('plugin-files') // e.g., 'plugins'
        .download(plugin.source_zip_url); // path inside bucket: e.g., 'zips/source.zip'

      if (error) {
        throw new Error(`Supabase Download Error: ${error.message}`);
      }

      // 2. Convert returned Blob/Data into a Buffer
      const arrayBuffer = await data.arrayBuffer();
      const buffer = Buffer.from(arrayBuffer);

      // 3. Unzip directly from the buffer using unzipper
      const directory = await unzipper.Open.buffer(buffer);
      const zip = new AdmZip(buffer);
      zip.extractAllTo(workDir, true /* overwrite */);

      // Helper function: Recursively crawl directory to collect all file and directory paths
      const getFilesAndDirs = (dir) => {
          let files = [];
          let dirs = new Set([dir]); // Include root workDir

          const entries = fs.readdirSync(dir, { withFileTypes: true });
          for (const entry of entries) {
              const fullPath = path.join(dir, entry.name);
              if (entry.isDirectory()) {
                  dirs.add(fullPath);
                  const nested = getFilesAndDirs(fullPath);
                  nested.files.forEach(f => files.push(f));
                  nested.dirs.forEach(d => dirs.add(d));
              } else if (entry.isFile()) {
                  files.push(fullPath);
              }
          }
          return { files, dirs: Array.from(dirs) };
      };

      // 3. Scan extracted contents
      const { files: allFiles, dirs: allDirs } = getFilesAndDirs(workDir);

      // 4. Collect all .cpp source files (quoted for spaces in paths)
      const cppFiles = allFiles
          .filter(filePath => filePath.endsWith('.cpp'))
          .map(filePath => `"${filePath}"`)
          .join(' ');

      if (!cppFiles) {
          return res.status(400).send('<pre>No .cpp files found in the zip archive.</pre>');
      }

      // 5. Build -I include flags for EVERY folder inside the zip
      const includeFlags = allDirs
          .map(dirPath => `-I "${dirPath}"`)
          .join(' ');

      // 6. Construct the cross-compilation command
      const outputDll = path.join(workDir, 'plugin.dll');
      const compileCmd = `x86_64-w64-mingw32-g++ -shared ${includeFlags} -o "${outputDll}" ${cppFiles}`;
      exec(compileCmd, async (error) => {
        if (error) {
          return res.status(500).send(`Compilation Failed:<br/><pre>${error.message}</pre>`);
        }

        const dllBytes = fs.readFileSync(outputDll);
        const dllStoragePath = `dlls/${id}_plugin.dll`;

        await supabase.storage
          .from('plugin-files')
          .upload(dllStoragePath, dllBytes, { contentType: 'application/octet-stream' });

        await supabase
          .from('plugins')
          .update({ state: 'approved', dll_url: dllStoragePath })
          .eq('id', id);

        fs.rmSync(workDir, { recursive: true, force: true });
        res.send('<h1>Plugin Approved! DLL Compiled and Published.</h1>');
      });
    }
  } catch (err) {
    res.status(500).send(`Error: ${err.message}`);
  }
});

/* ==========================================================================
   4. DEVELOPER SOURCE ZIP DOWNLOAD ENDPOINT
   ========================================================================== */
app.get('/api/plugins/download-source', async (req, res) => {
  try {
    // Read the internal storage path passed via URL query parameter (e.g., ?path=sources/12345_plugin.zip)
    const storagePath = req.query.path;

    if (!storagePath) {
      return res.status(400).send('Storage path parameter is required.');
    }

    // Download file buffer directly using official Supabase SDK
    const { data, error } = await supabase.storage
      .from('plugin-files')
      .download(storagePath);

    if (error || !data) {
      return res.status(404).send(`Failed to fetch source archive: ${error ? error.message : 'File not found'}`);
    }

    // Convert Blob to Node.js Buffer for streaming
    const arrayBuffer = await data.arrayBuffer();
    const buffer = Buffer.from(arrayBuffer);

    // Set HTTP response headers to trigger file download in browser
    const filename = path.basename(storagePath);
    res.setHeader('Content-Type', 'application/zip');
    res.setHeader('Content-Disposition', `attachment; filename="${filename}"`);
    res.setHeader('Content-Length', buffer.length);

    // Send binary buffer to client browser
    res.send(buffer);
  } catch (err) {
    res.status(500).send(`Server error: ${err.message}`);
  }
});

/* ==========================================================================
   5. DLL DOWNLOAD ENDPOINT (FOR CPR CLIENT)
   ========================================================================== */
app.get('/api/plugins/download-dll', async (req, res) => {
  try {
    const pluginId = req.query.id;

    if (!pluginId) {
      return res.status(400).json({ error: 'Plugin ID parameter is required.' });
    }

    // Look up the plugin to get its stored state and DLL path
    const { data: plugin, error: dbErr } = await supabase
      .from('plugins')
      .select('state, dll_url, name')
      .eq('id', pluginId)
      .single();

    if (dbErr || !plugin) {
      return res.status(404).json({ error: 'Plugin not found.' });
    }

    if (plugin.state !== 'approved' || !plugin.dll_url) {
      return res.status(400).json({ error: 'Plugin DLL is not available or not yet approved.' });
    }

    // Download the binary file directly from Supabase Storage SDK
    // (plugin.dll_url stores the storage path, e.g., "dlls/123_plugin.dll")
    const { data: fileBlob, error: storageErr } = await supabase.storage
      .from('plugin-files')
      .download(plugin.dll_url);

    if (storageErr || !fileBlob) {
      return res.status(500).json({ error: `Storage download error: ${storageErr.message}` });
    }

    const arrayBuffer = await fileBlob.arrayBuffer();
    const buffer = Buffer.from(arrayBuffer);

    // Set binary stream headers so CPR receives clean, uncorrupted bytes
    res.setHeader('Content-Type', 'application/octet-stream');
    res.setHeader('Content-Disposition', `attachment; filename="${plugin.name}.dll"`);
    res.setHeader('Content-Length', buffer.length);

    // Send the raw binary buffer
    res.send(buffer);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

/* ==========================================================================
   6. CLIENT STORE SEARCH & DOWNLOAD
   ========================================================================== */
app.get('/api/store/search', async (req, res) => {
  const query = req.query.q || '';

  const { data, error } = await supabase
    .from('plugins')
    .select('id, name, creator, description, created_at, dll_url')
    .eq('state', 'approved')
    .ilike('name', `%${query}%`);

  if (error) {
    console.log(error)
    return res.status(500).json({ error: error.message })};
  res.json(data);
});

/* ==========================================================================
   7. DEVELOPER VERIFICATION ENDPOINT
   ========================================================================== */
app.get('/api/developers/verify', async (req, res) => {
  try {
    const { username, api_key } = req.query;

    // Validate query parameters
    if (!username || !api_key) {
      return res.status(400).send('No');
    }

    // Query the database for a developer matching BOTH username and api_key
    const { data: dev, error } = await supabase
      .from('developers')
      .select('id')
      .eq('username', username)
      .eq('api_key', api_key)
      .maybeSingle();

    // If query fails or no matching developer record exists, return "No"
    if (error || !dev) {
      return res.send('No');
    }

    // Both username exists and api_key matches correctly
    return res.send('Yes');
  } catch (err) {
    res.status(500).send('No');
  }
});

const PORT = process.env.PORT || 3000;
app.listen(PORT, () => console.log(`Server listening on port ${PORT}`));
