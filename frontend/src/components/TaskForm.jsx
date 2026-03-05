import { useState } from 'react';

export default function TaskForm({ task, allTasks = [], onSave, onCancel }) {
  const [form, setForm] = useState({
    title: task.title || '',
    description: task.description || '',
    priority: task.priority || 3,
    estimated_minutes: task.estimated_minutes || 30,
    category: task.category || '',
    status: task.status || 'todo',
    due_date: task.due_date || '',
    parent_id: task.parent_id || 0,
  });

  const set = (field) => (e) => setForm({ ...form, [field]: e.target.value });

  const handleSubmit = (e) => {
    e.preventDefault();
    onSave({
      ...form,
      id: task.id || undefined,
      priority: Number(form.priority),
      estimated_minutes: Number(form.estimated_minutes),
      parent_id: Number(form.parent_id),
    });
  };

  // Exclude the task being edited (and its descendants) from parent options
  const getDescendantIds = (id) => {
    const ids = new Set([id]);
    const collect = (parentId) => {
      for (const t of allTasks) {
        if (t.parent_id === parentId && !ids.has(t.id)) {
          ids.add(t.id);
          collect(t.id);
        }
      }
    };
    collect(id);
    return ids;
  };

  const excludeIds = task.id ? getDescendantIds(task.id) : new Set();
  const parentOptions = allTasks.filter(t => !excludeIds.has(t.id));

  // Build indented labels for parent selector
  const getDepth = (t) => {
    let depth = 0;
    let current = t;
    while (current && current.parent_id) {
      depth++;
      current = allTasks.find(p => p.id === current.parent_id);
    }
    return depth;
  };

  return (
    <div className="modal-overlay" onClick={onCancel}>
      <div className="modal" onClick={e => e.stopPropagation()}>
        <h3>{task.id ? 'Edit Task' : task.parent_id ? 'New Subtask' : 'New Task'}</h3>
        <form onSubmit={handleSubmit}>
          <div className="form-group">
            <label>Parent Task</label>
            <select value={form.parent_id} onChange={set('parent_id')}>
              <option value={0}>-- None (root task) --</option>
              {parentOptions.map(t => (
                <option key={t.id} value={t.id}>
                  {'\u00A0\u00A0'.repeat(getDepth(t))}{t.title}
                </option>
              ))}
            </select>
          </div>
          <div className="form-group">
            <label>Title</label>
            <input value={form.title} onChange={set('title')} required />
          </div>
          <div className="form-group">
            <label>Description</label>
            <textarea value={form.description} onChange={set('description')} />
          </div>
          <div className="form-row">
            <div className="form-group">
              <label>Priority</label>
              <select value={form.priority} onChange={set('priority')}>
                <option value={1}>1 - Critical</option>
                <option value={2}>2 - High</option>
                <option value={3}>3 - Medium</option>
                <option value={4}>4 - Low</option>
                <option value={5}>5 - Minimal</option>
              </select>
            </div>
            <div className="form-group">
              <label>Estimated (min)</label>
              <input type="number" min="5" step="5" value={form.estimated_minutes}
                     onChange={set('estimated_minutes')} />
            </div>
          </div>
          <div className="form-row">
            <div className="form-group">
              <label>Category</label>
              <input value={form.category} onChange={set('category')}
                     placeholder="e.g. Work, Personal" />
            </div>
            <div className="form-group">
              <label>Due Date</label>
              <input type="date" value={form.due_date} onChange={set('due_date')} />
            </div>
          </div>
          <div className="form-group">
            <label>Status</label>
            <select value={form.status} onChange={set('status')}>
              <option value="todo">To Do</option>
              <option value="in_progress">In Progress</option>
              <option value="done">Done</option>
            </select>
          </div>
          <div className="modal-actions">
            <button type="button" className="btn" onClick={onCancel}>Cancel</button>
            <button type="submit" className="btn btn-primary">
              {task.id ? 'Update' : 'Create'}
            </button>
          </div>
        </form>
      </div>
    </div>
  );
}
