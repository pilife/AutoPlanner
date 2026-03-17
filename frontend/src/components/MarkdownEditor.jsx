import { useState, useCallback, useRef } from 'react';
import MDEditor from '@uiw/react-md-editor';
import { getUploadSas, uploadToBlob } from '../api';

export default function MarkdownEditor({ value, onChange, readOnly = false }) {
  const [uploading, setUploading] = useState(false);
  const editorRef = useRef(null);

  const handleImageUpload = useCallback(async (file) => {
    if (!file || !file.type.startsWith('image/')) return null;
    if (file.size > 5 * 1024 * 1024) {
      alert('Image too large (max 5MB)');
      return null;
    }

    setUploading(true);
    try {
      const { upload_url, blob_url } = await getUploadSas(file.name, file.type);
      await uploadToBlob(upload_url, file);
      return blob_url;
    } catch (err) {
      console.error('Image upload failed:', err);
      alert('Image upload failed');
      return null;
    } finally {
      setUploading(false);
    }
  }, []);

  const handlePaste = useCallback(async (e) => {
    const items = e.clipboardData?.items;
    if (!items) return;

    for (const item of items) {
      if (item.type.startsWith('image/')) {
        e.preventDefault();
        const file = item.getAsFile();
        const url = await handleImageUpload(file);
        if (url) {
          const markdown = `![image](${url})`;
          const current = value || '';
          // Insert at cursor position or append
          const textarea = e.target;
          if (textarea && textarea.selectionStart !== undefined) {
            const start = textarea.selectionStart;
            const newValue = current.slice(0, start) + markdown + current.slice(start);
            onChange(newValue);
          } else {
            onChange(current + '\n' + markdown);
          }
        }
        return;
      }
    }
  }, [value, onChange, handleImageUpload]);

  const handleDrop = useCallback(async (e) => {
    const files = e.dataTransfer?.files;
    if (!files || files.length === 0) return;

    const file = files[0];
    if (!file.type.startsWith('image/')) return;

    e.preventDefault();
    const url = await handleImageUpload(file);
    if (url) {
      onChange((value || '') + `\n![image](${url})`);
    }
  }, [value, onChange, handleImageUpload]);

  if (readOnly) {
    return (
      <div data-color-mode="light">
        <MDEditor.Markdown source={value || ''} style={{ padding: 8 }} />
      </div>
    );
  }

  return (
    <div
      data-color-mode="light"
      onPaste={handlePaste}
      onDrop={handleDrop}
      onDragOver={(e) => e.preventDefault()}
      style={{ position: 'relative' }}
    >
      <MDEditor
        ref={editorRef}
        value={value || ''}
        onChange={onChange}
        preview="edit"
        height={150}
        visibleDragbar={false}
      />
      {uploading && (
        <div style={{
          position: 'absolute', inset: 0,
          background: 'rgba(255,255,255,0.8)',
          display: 'flex', alignItems: 'center', justifyContent: 'center',
          borderRadius: 6, fontSize: '0.85rem', color: '#636e72'
        }}>
          Uploading image...
        </div>
      )}
    </div>
  );
}
