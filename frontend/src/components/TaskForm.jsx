import { useState } from 'react';
import { toMinutes, fromMinutes } from '../helpers';
import MarkdownEditor from './MarkdownEditor';

export default function TaskForm({ task, allTasks = [], onSave, onCancel }) {
  const isLeaf = !task.id || !allTasks.some(t => t.parent_id === task.id);
  const [form, setForm] = useState({
    title: task.title || '',
    description: task.description || '',
    priority: task.priority || 3,
    estimatedValue: fromMinutes(task.estimated_minutes || 60, 'hours'),
    estimatedUnit: 'hours',
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
      estimated_minutes: toMinutes(form.estimatedValue, form.estimatedUnit),
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
            <MarkdownEditor
              value={form.description}
              onChange={(val) => setForm(f => ({ ...f, description: val || '' }))}
            />
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
              <label>Estimated Time</label>
              {isLeaf ? (
                <div style={{ display: 'flex', gap: 6 }}>
                  <input type="number" min="0.5" step="0.5" value={form.estimatedValue}
                         onChange={set('estimatedValue')} style={{ flex: 1 }} />
                  <select value={form.estimatedUnit} onChange={set('estimatedUnit')} style={{ width: 90 }}>
                    <option value="minutes">min</option>
                    <option value="hours">hours</option>
                    <option value="days">days</option>
                  </select>
                </div>
              ) : (
                <div style={{ color: '#636e72', fontSize: '0.85rem', padding: '6px 0' }}>
                  Estimated time is derived from subtasks
                </div>
              )}
            </div>
          </div>
          <div className="form-row">
            <div className="form-group">
              <label>Category</label>
              <select value={form.category} onChange={set('category')}>
                <option value="">N/A</option>
                <option value="Design">Design</option>
                <option value="Coding">Coding</option>
                <option value="Test">Test</option>
                <option value="Monitor">Monitor</option>
              </select>
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
