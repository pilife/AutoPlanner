import { useState, useEffect } from 'react';
import { getTasks, createTask, updateTask, deleteTask } from '../api';
import TaskForm from './TaskForm';
import { formatDuration } from '../helpers';

const PRIORITY_LABELS = { 1: 'Critical', 2: 'High', 3: 'Medium', 4: 'Low', 5: 'Minimal' };

function buildTree(tasks) {
  const map = {};
  const roots = [];
  for (const t of tasks) {
    map[t.id] = { ...t, children: [] };
  }
  for (const t of tasks) {
    if (t.parent_id && map[t.parent_id]) {
      map[t.parent_id].children.push(map[t.id]);
    } else {
      roots.push(map[t.id]);
    }
  }
  return roots;
}

function TaskRow({ task, depth, onEdit, onDelete, onStatusToggle, onAddChild, onArchiveToggle, expandedIds, onToggleExpand }) {
  const hasChildren = task.children && task.children.length > 0;
  const expanded = expandedIds.has(task.id);
  const isArchived = task.archived === 1 || task.archived === true;

  return (
    <>
      <tr className="task-row" style={isArchived ? { opacity: 0.55 } : {}}>
        <td style={{ paddingLeft: 12 + depth * 24 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
            {hasChildren ? (
              <button className="tree-toggle" onClick={() => onToggleExpand(task.id)}>
                {expanded ? '\u25BE' : '\u25B8'}
              </button>
            ) : (
              <span style={{ width: 16, display: 'inline-block' }} />
            )}
            <span>{task.title}</span>
            {isArchived && (
              <span style={{ fontSize: '0.7rem', color: '#b2bec3', marginLeft: 6, fontStyle: 'italic' }}>archived</span>
            )}
          </div>
        </td>
        <td>{task.category || '-'}</td>
        <td><span className={`priority priority-${task.priority}`} style={{ marginRight: 6 }} />{PRIORITY_LABELS[task.priority]}</td>
        <td>{formatDuration(task.estimated_minutes)}</td>
        <td>{task.due_date || '-'}</td>
        <td>
          <span
            className={`status-badge status-${task.status}`}
            style={{ cursor: 'pointer' }}
            onClick={() => onStatusToggle(task)}
          >
            {task.status.replace('_', ' ')}
          </span>
        </td>
        <td>
          <div style={{ display: 'flex', gap: 4 }}>
            <button className="btn btn-sm" onClick={() => onAddChild(task)} title="Add subtask">+</button>
            <button className="btn btn-sm" onClick={() => onEdit(task)}>Edit</button>
            {task.status === 'done' && (
              <button
                className="btn btn-sm"
                onClick={() => onArchiveToggle(task)}
                title={isArchived ? 'Unarchive' : 'Archive'}
              >
                {isArchived ? 'Unarchive' : 'Archive'}
              </button>
            )}
            <button className="btn btn-sm btn-danger" onClick={() => onDelete(task.id)}>Del</button>
          </div>
        </td>
      </tr>
      {hasChildren && expanded && task.children.map(child => (
        <TaskRow
          key={child.id}
          task={child}
          depth={depth + 1}
          onEdit={onEdit}
          onDelete={onDelete}
          onStatusToggle={onStatusToggle}
          onAddChild={onAddChild}
          onArchiveToggle={onArchiveToggle}
          expandedIds={expandedIds}
          onToggleExpand={onToggleExpand}
        />
      ))}
    </>
  );
}

const EXPAND_STORAGE_KEY = 'autoplanner_task_expanded';

function TaskTree({ tree, allTasks, onEdit, onDelete, onStatusToggle, onAddChild, onArchiveToggle }) {
  const [expandedIds, setExpandedIds] = useState(() => {
    try {
      const saved = JSON.parse(localStorage.getItem(EXPAND_STORAGE_KEY));
      if (Array.isArray(saved) && saved.length > 0) return new Set(saved);
    } catch (_) {}
    return new Set();
  });

  // On first load expand all non-done parent nodes; also scrub done task IDs
  // from any existing saved state so done tasks' children fold automatically
  useEffect(() => {
    const doneIds = new Set(allTasks.filter(t => t.status === 'done').map(t => t.id));
    setExpandedIds(prev => {
      if (prev.size === 0) {
        const ids = new Set();
        const collect = (nodes) => {
          for (const n of nodes) {
            if (n.children && n.children.length > 0 && n.status !== 'done') {
              ids.add(n.id);
              collect(n.children);
            }
          }
        };
        collect(tree);
        return ids;
      }
      // Remove done task IDs from saved state
      let changed = false;
      const next = new Set();
      for (const id of prev) {
        if (doneIds.has(id)) changed = true;
        else next.add(id);
      }
      return changed ? next : prev;
    });
  }, [allTasks]);

  // Persist to localStorage whenever expandedIds changes
  useEffect(() => {
    localStorage.setItem(EXPAND_STORAGE_KEY, JSON.stringify([...expandedIds]));
  }, [expandedIds]);

  const toggleExpand = (id) => {
    setExpandedIds(prev => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  };


  return (
    <div className="card">
      <table className="task-table">
        <thead>
          <tr>
            <th>Title</th>
            <th>Category</th>
            <th>Priority</th>
            <th>Est.</th>
            <th>Due</th>
            <th>Status</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          {tree.map(t => (
            <TaskRow
              key={t.id}
              task={t}
              depth={0}
              onEdit={onEdit}
              onDelete={onDelete}
              onStatusToggle={onStatusToggle}
              onAddChild={onAddChild}
              onArchiveToggle={onArchiveToggle}
              expandedIds={expandedIds}
              onToggleExpand={toggleExpand}
            />
          ))}
        </tbody>
      </table>
    </div>
  );
}

export default function TaskList() {
  const [tasks, setTasks] = useState([]);
  const [editing, setEditing] = useState(null); // null = closed, {} = new, task = edit
  const [error, setError] = useState('');
  const [showArchived, setShowArchived] = useState(false);

  const load = async () => {
    try {
      const params = showArchived ? { include_archived: '1' } : {};
      setTasks(await getTasks(params));
    } catch (e) {
      setError(e.message);
    }
  };

  useEffect(() => { load(); }, [showArchived]);

  const handleSave = async (task) => {
    try {
      if (task.id) {
        await updateTask(task.id, task);
      } else {
        await createTask(task);
      }
      setEditing(null);
      load();
    } catch (e) {
      setError(e.message);
    }
  };

  const handleDelete = async (id) => {
    if (!confirm('Delete this task and all its subtasks?')) return;
    try {
      await deleteTask(id);
      load();
    } catch (e) {
      setError(e.message);
    }
  };

  const handleStatusToggle = async (task) => {
    const next = { todo: 'in_progress', in_progress: 'done', done: 'todo' };
    await updateTask(task.id, { status: next[task.status] });
    load();
  };

  const handleAddChild = (parentTask) => {
    setEditing({ parent_id: parentTask.id, category: parentTask.category, priority: parentTask.priority });
  };

  const handleArchiveToggle = async (task) => {
    const isArchived = task.archived === 1 || task.archived === true;
    try {
      await updateTask(task.id, { archived: isArchived ? 0 : 1 });
      load();
    } catch (e) {
      setError(e.message);
    }
  };

  const tree = buildTree(tasks);

  return (
    <div>
      <div className="page-header">
        <h2>Tasks</h2>
        <div style={{ display: 'flex', gap: 10, alignItems: 'center' }}>
          <label style={{ display: 'flex', alignItems: 'center', gap: 6, fontSize: '0.9rem', color: '#636e72' }}>
            <input
              type="checkbox"
              checked={showArchived}
              onChange={e => setShowArchived(e.target.checked)}
            />
            Show archived
          </label>
          <button className="btn btn-primary" onClick={() => setEditing({})}>+ New Task</button>
        </div>
      </div>

      {error && <div style={{ color: '#d63031', marginBottom: 12 }}>{error}</div>}

      {tasks.length === 0 ? (
        <div className="empty">No tasks yet. Create one to get started.</div>
      ) : (
        <TaskTree
          tree={tree}
          allTasks={tasks}
          onEdit={setEditing}
          onDelete={handleDelete}
          onStatusToggle={handleStatusToggle}
          onAddChild={handleAddChild}
          onArchiveToggle={handleArchiveToggle}
        />
      )}

      {editing !== null && (
        <TaskForm
          task={editing}
          allTasks={tasks}
          onSave={handleSave}
          onCancel={() => setEditing(null)}
        />
      )}
    </div>
  );
}
