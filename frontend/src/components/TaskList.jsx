import { useState, useEffect } from 'react';
import { getTasks, createTask, updateTask, deleteTask } from '../api';
import TaskForm from './TaskForm';

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

function TaskRow({ task, depth, onEdit, onDelete, onStatusToggle, onAddChild, expanded, onToggleExpand }) {
  const hasChildren = task.children && task.children.length > 0;

  return (
    <>
      <tr className="task-row">
        <td style={{ paddingLeft: 12 + depth * 24 }}>
          <span className={`priority priority-${task.priority}`} />
        </td>
        <td style={{ paddingLeft: depth * 24 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
            {hasChildren ? (
              <button className="tree-toggle" onClick={() => onToggleExpand(task.id)}>
                {expanded ? '\u25BE' : '\u25B8'}
              </button>
            ) : (
              <span style={{ width: 16, display: 'inline-block' }} />
            )}
            <span>{task.title}</span>
          </div>
        </td>
        <td>{task.category || '-'}</td>
        <td>{PRIORITY_LABELS[task.priority]}</td>
        <td>{task.estimated_minutes}m</td>
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
          expanded={expanded}
          onToggleExpand={onToggleExpand}
        />
      ))}
    </>
  );
}

function TaskTree({ tree, allTasks, onEdit, onDelete, onStatusToggle, onAddChild }) {
  const [expandedIds, setExpandedIds] = useState(new Set());

  // Auto-expand all nodes that have children on initial load
  useEffect(() => {
    const ids = new Set();
    const collect = (nodes) => {
      for (const n of nodes) {
        if (n.children && n.children.length > 0) {
          ids.add(n.id);
          collect(n.children);
        }
      }
    };
    collect(tree);
    setExpandedIds(ids);
  }, [allTasks]);

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
            <th></th>
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
              expanded={expandedIds.has(t.id)}
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
  const [filter, setFilter] = useState({ status: '', category: '' });
  const [editing, setEditing] = useState(null); // null = closed, {} = new, task = edit
  const [error, setError] = useState('');

  const load = async () => {
    try {
      const params = {};
      if (filter.status) params.status = filter.status;
      if (filter.category) params.category = filter.category;
      setTasks(await getTasks(params));
    } catch (e) {
      setError(e.message);
    }
  };

  useEffect(() => { load(); }, [filter]);

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

  const tree = buildTree(tasks);

  return (
    <div>
      <div className="page-header">
        <h2>Tasks</h2>
        <button className="btn btn-primary" onClick={() => setEditing({})}>+ New Task</button>
      </div>

      {error && <div style={{ color: '#d63031', marginBottom: 12 }}>{error}</div>}

      <div className="filters">
        <select value={filter.status} onChange={e => setFilter(f => ({ ...f, status: e.target.value }))}>
          <option value="">All statuses</option>
          <option value="todo">To Do</option>
          <option value="in_progress">In Progress</option>
          <option value="done">Done</option>
        </select>
      </div>

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
