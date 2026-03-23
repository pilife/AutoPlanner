import { useState, useEffect } from 'react';
import { useParams } from 'react-router-dom';
import MDEditor from '@uiw/react-md-editor';
import { getPlan, savePlan, generateWeeklyPlan, generateDailyPlans, getTasks, getUnreviewedPlans, updateTask, createTask } from '../api';
import { formatDuration, getTaskPath } from '../helpers';
import ReviewModal from './ReviewModal';

function todayStr() {
  return new Date().toISOString().slice(0, 10);
}

function getMondayOfWeek(dateStr) {
  const d = new Date(dateStr + 'T00:00:00');
  const day = d.getDay();
  const diff = (day + 6) % 7;
  d.setDate(d.getDate() - diff);
  return d.toISOString().slice(0, 10);
}

function getWeekDays(monday) {
  const days = [];
  const d = new Date(monday + 'T00:00:00');
  for (let i = 0; i < 5; i++) {
    days.push(d.toISOString().slice(0, 10));
    d.setDate(d.getDate() + 1);
  }
  return days;
}

const DAY_NAMES = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday'];

const PRIORITY_LABELS = { 1: 'Critical', 2: 'High', 3: 'Medium', 4: 'Low', 5: 'Minimal' };

function TaskDetailModal({ task, taskMap, onClose }) {
  if (!task) return null;
  const path = task.parent_id ? getTaskPath(task.id, taskMap) : null;
  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal" onClick={e => e.stopPropagation()} style={{ maxWidth: 600 }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', marginBottom: 12 }}>
          <h3 style={{ margin: 0 }}>{task.title}</h3>
          <button className="btn btn-sm" onClick={onClose} style={{ fontSize: '1.1rem', lineHeight: 1 }}>&times;</button>
        </div>
        {path && <div style={{ fontSize: '0.8rem', color: '#636e72', marginBottom: 12 }}>{path}</div>}
        <div style={{ display: 'flex', gap: 12, flexWrap: 'wrap', marginBottom: 16, fontSize: '0.85rem' }}>
          <span><strong>Priority:</strong> {PRIORITY_LABELS[task.priority] || task.priority}</span>
          {task.category && <span><strong>Category:</strong> {task.category}</span>}
          <span><strong>Status:</strong> {task.status}</span>
          {task.estimated_minutes > 0 && <span><strong>Estimate:</strong> {formatDuration(task.estimated_minutes)}</span>}
        </div>
        {task.description && (
          <div data-color-mode="light" style={{ maxHeight: 400, overflow: 'auto' }}>
            <MDEditor.Markdown source={task.description} />
          </div>
        )}
      </div>
    </div>
  );
}

// Build hierarchy tree from flat plan items
function buildPlanTree(items, taskMap) {
  // Find root ancestor for a task
  const getRootId = (taskId) => {
    let current = taskMap[taskId];
    while (current && current.parent_id && taskMap[current.parent_id]) {
      current = taskMap[current.parent_id];
    }
    return current ? current.id : taskId;
  };

  // Group items by root ancestor
  const groups = {};
  const rootOrder = [];
  for (const item of items) {
    const rootId = getRootId(item.task_id);
    if (!groups[rootId]) {
      groups[rootId] = [];
      rootOrder.push(rootId);
    }
    groups[rootId].push(item);
  }

  const result = rootOrder.map(rootId => ({
    rootId,
    rootTask: taskMap[rootId],
    items: groups[rootId],
  }));
  // Sort groups by root task priority (ascending), then by task id for stability
  result.sort((a, b) => {
    const pa = a.rootTask?.priority || 3;
    const pb = b.rootTask?.priority || 3;
    return pa !== pb ? pa - pb : (a.rootId - b.rootId);
  });
  // Sort items within each group by leaf task priority
  for (const group of result) {
    group.items.sort((a, b) => {
      const pa = taskMap[a.task_id]?.priority || 3;
      const pb = taskMap[b.task_id]?.priority || 3;
      return pa !== pb ? pa - pb : (a.task_id - b.task_id);
    });
  }
  return result;
}

// Build a nested tree for a group's items, including intermediate parent nodes
function buildNestedTree(items, rootId, taskMap) {
  const planTaskIds = new Set(items.map(it => it.task_id));
  const itemByTaskId = {};
  for (const it of items) itemByTaskId[it.task_id] = it;

  // Collect all ancestor chains
  const nodeIds = new Set();
  for (const item of items) {
    let cur = taskMap[item.task_id];
    while (cur) {
      nodeIds.add(cur.id);
      cur = cur.parent_id && taskMap[cur.parent_id] ? taskMap[cur.parent_id] : null;
    }
  }

  // Build children map
  const childrenOf = {};
  for (const id of nodeIds) {
    const task = taskMap[id];
    const parentId = task && task.parent_id && nodeIds.has(task.parent_id) ? task.parent_id : null;
    if (parentId) {
      if (!childrenOf[parentId]) childrenOf[parentId] = [];
      childrenOf[parentId].push(id);
    }
  }

  // Sort children by priority for consistent ordering
  for (const parentId in childrenOf) {
    childrenOf[parentId].sort((a, b) => {
      const pa = taskMap[a]?.priority || 3;
      const pb = taskMap[b]?.priority || 3;
      return pa !== pb ? pa - pb : (a - b);
    });
  }

  // Recursive build
  const buildNode = (id) => {
    const children = (childrenOf[id] || []).map(buildNode);
    return {
      id,
      task: taskMap[id],
      planItem: itemByTaskId[id] || null,
      isLeaf: planTaskIds.has(id),
      children,
    };
  };

  return buildNode(rootId);
}

function DailyPlansDragGrid({ dailyPlans, taskMap, onPlansUpdated, onTaskClick }) {
  const [localPlans, setLocalPlans] = useState(dailyPlans);
  const [dragItem, setDragItem] = useState(null);
  const [dragOverDate, setDragOverDate] = useState(null);
  const today = todayStr();

  useEffect(() => { setLocalPlans(dailyPlans); }, [dailyPlans]);

  const handleDragStart = (e, date, itemIndex, item) => {
    setDragItem({ fromDate: date, itemIndex, item });
    e.dataTransfer.effectAllowed = 'move';
  };

  const handleDragOver = (e, date) => {
    if (date < today) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = 'move';
    setDragOverDate(date);
  };

  const handleDragLeave = () => {
    setDragOverDate(null);
  };

  const handleDrop = async (e, toDate) => {
    e.preventDefault();
    setDragOverDate(null);
    if (!dragItem || dragItem.fromDate === toDate || toDate < today) return;

    const fromDate = dragItem.fromDate;
    const fromPlan = localPlans.find(dp => dp.date === fromDate)?.plan;
    const toPlan = localPlans.find(dp => dp.date === toDate)?.plan;
    if (!fromPlan) return;

    const newFromItems = fromPlan.items.filter((_, i) => i !== dragItem.itemIndex);
    const newToItems = [...(toPlan?.items || []), dragItem.item];

    // Optimistic update
    setLocalPlans(prev => prev.map(dp => {
      if (dp.date === fromDate) return { ...dp, plan: { ...fromPlan, items: newFromItems } };
      if (dp.date === toDate) return { ...dp, plan: { ...(toPlan || { type: 'daily', date: toDate }), items: newToItems } };
      return dp;
    }));

    setDragItem(null);

    // Save in background
    try {
      await savePlan({ ...fromPlan, items: newFromItems });
      await savePlan(toPlan ? { ...toPlan, items: newToItems } : { type: 'daily', date: toDate, items: newToItems });
    } catch (err) {
      setLocalPlans(dailyPlans); // revert on error
      alert('Failed to move task: ' + err.message);
    }
  };

  const handleDragEnd = () => {
    setDragItem(null);
    setDragOverDate(null);
  };

  return (
    <div>
      <h3 style={{ marginBottom: 16 }}>Daily Plans</h3>
      <p style={{ color: '#636e72', fontSize: '0.8rem', marginBottom: 12 }}>
        Drag tasks between days to reschedule (past days are locked). Click a task to view details.
      </p>
      <div className="daily-plans-grid">
        {localPlans.map((dp, i) => {
          const isPast = dp.date < today;
          const isDropTarget = dragOverDate === dp.date;

          return (
            <div
              key={dp.date}
              className={`card daily-plan-card ${isPast ? 'daily-plan-past' : ''} ${isDropTarget ? 'daily-plan-drop-target' : ''}`}
              onDragOver={e => handleDragOver(e, dp.date)}
              onDragLeave={handleDragLeave}
              onDrop={e => handleDrop(e, dp.date)}
            >
              <h4 style={{ marginBottom: 10 }}>
                {DAY_NAMES[i]} &mdash; {dp.date}
                {isPast && <span style={{ fontSize: '0.75rem', color: '#b2bec3', marginLeft: 8 }}>(locked)</span>}
              </h4>
              {!dp.plan || dp.plan.items.length === 0 ? (
                <div className="daily-plan-empty">No tasks</div>
              ) : (
                (() => {
                  const groups = buildPlanTree(dp.plan.items, taskMap);
                  return groups.map(group => {
                    const groupMinutes = group.items.reduce((s, it) => s + it.duration_minutes, 0);
                    return (
                      <div key={group.rootId} style={{ marginBottom: 8 }}>
                        <div style={{ fontSize: '0.8rem', fontWeight: 600, color: '#2d3436', padding: '4px 0', borderBottom: '1px solid #eee', marginBottom: 4, display: 'flex', justifyContent: 'space-between' }}>
                          <span>{group.rootTask?.title || `Task #${group.rootId}`}</span>
                          <span style={{ color: '#636e72', fontWeight: 400 }}>{formatDuration(groupMinutes)}</span>
                        </div>
                        <div className="plan-timeline" style={{ gap: 4 }}>
                          {group.items.map(item => {
                            const task = taskMap[item.task_id];
                            const flatIdx = dp.plan.items.findIndex(it => it.task_id === item.task_id);
                            const isLeafRoot = item.task_id === group.rootId;
                            return (
                              <div
                                key={`${item.task_id}-${flatIdx}`}
                                className={`plan-slot ${!isPast ? 'plan-slot-draggable' : ''}`}
                                style={{ paddingLeft: isLeafRoot ? 16 : 28 }}
                                draggable={!isPast}
                                onDragStart={e => !isPast && handleDragStart(e, dp.date, flatIdx, item)}
                                onDragEnd={handleDragEnd}
                              >
                                {!isPast && <span className="drag-handle">{'\u2630'}</span>}
                                <div style={{ flex: 1, cursor: 'pointer' }} onClick={() => task && onTaskClick(task)}>
                                  <strong style={{ fontSize: '0.85rem' }}>
                                    {task && task.parent_id > 0 ? getTaskPath(task.id, taskMap) : (task?.title || `Task #${item.task_id}`)}
                                  </strong>
                                </div>
                                <span className="duration">{formatDuration(item.duration_minutes)}</span>
                              </div>
                            );
                          })}
                        </div>
                      </div>
                    );
                  });
                })()
              )}
            </div>
          );
        })}
      </div>
    </div>
  );
}

export default function PlanView() {
  const { type } = useParams();
  const [date, setDate] = useState(todayStr());
  const [plan, setPlan] = useState(null);
  const [dailyPlans, setDailyPlans] = useState([]);
  const [dailyWarning, setDailyWarning] = useState('');
  const [taskMap, setTaskMap] = useState({});
  const [allTasks, setAllTasks] = useState([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  const [showAddTask, setShowAddTask] = useState(false);
  const [unreviewedPlans, setUnreviewedPlans] = useState([]);
  const [selectedTask, setSelectedTask] = useState(null);
  const [splittingTaskId, setSplittingTaskId] = useState(null);
  const [splitNames, setSplitNames] = useState(['', '']);

  const monday = getMondayOfWeek(date);

  const load = async () => {
    setLoading(true);
    setError('');
    setDailyWarning('');
    try {
      const tasks = await getTasks();
      const map = {};
      for (const t of tasks) map[t.id] = t;
      setTaskMap(map);
      setAllTasks(tasks);

      if (type === 'weekly') {
        const planData = await getPlan('weekly', date);
        setPlan(planData && planData.id ? planData : null);
        const weekDays = getWeekDays(monday);
        const dailies = await Promise.all(weekDays.map(d => getPlan('daily', d)));
        setDailyPlans(dailies.map((dp, i) => ({
          date: weekDays[i],
          plan: dp && dp.id ? dp : null,
        })));
      } else {
        const planData = await getPlan('daily', date);
        setPlan(planData && planData.id ? planData : null);
        setDailyPlans([]);
        // Check for unreviewed past daily plans
        try {
          const today = todayStr();
          const unreviewed = await getUnreviewedPlans(today);
          setUnreviewedPlans(unreviewed.filter(p => p.items && p.items.length > 0));
        } catch (_) {
          setUnreviewedPlans([]);
        }
        // Check for overload warning by looking at weekly context
        try {
          const weeklyPlan = await getPlan('weekly', date);
          if (weeklyPlan && weeklyPlan.id) {
            const today = todayStr();
            const weekDays = getWeekDays(getMondayOfWeek(date));
            const remainingDays = weekDays.filter(d => d >= today).length;
            const dailyCapacity = 480;
            let remaining = 0;
            for (const item of weeklyPlan.items) {
              const t = map[item.task_id];
              if (t && t.status !== 'done') remaining += item.duration_minutes;
            }
            const totalCap = remainingDays * dailyCapacity;
            if (remaining > totalCap) {
              setDailyWarning(
                `Overloaded: ${formatDuration(remaining - totalCap)} of work exceeds remaining capacity (${remainingDays} days left). Consider adjusting your weekly plan.`
              );
            } else if (remaining > totalCap * 0.9) {
              setDailyWarning(
                `Near capacity: workload is over 90% of remaining capacity (${remainingDays} days left). Consider adjusting priorities.`
              );
            }
          }
        } catch (_) {}
      }
    } catch (e) {
      setError(e.message);
    }
    setLoading(false);
  };

  useEffect(() => { load(); }, [type, date]);

  const handleGenerateWeekly = async () => {
    setLoading(true);
    try {
      await generateWeeklyPlan(date);
      load();
    } catch (e) {
      setError(e.message);
    }
    setLoading(false);
  };

  const handleReviewComplete = () => {
    setUnreviewedPlans([]);
    load();
  };

  const handleReviewSkip = () => {
    setUnreviewedPlans([]);
  };

  const handleGenerateDaily = async () => {
    setLoading(true);
    try {
      const result = await generateDailyPlans(date);
      if (result.warning) setDailyWarning(result.warning);
      load();
    } catch (e) {
      setError(e.message);
    }
    setLoading(false);
  };

  const handleRemoveItem = async (index) => {
    if (!plan) return;
    const newItems = plan.items.filter((_, i) => i !== index);
    try {
      await savePlan({ ...plan, items: newItems });
      load();
    } catch (e) {
      setError(e.message);
    }
  };

  const handleAddTask = async (taskId) => {
    if (!plan) return;
    const task = taskMap[taskId];
    if (!task) return;
    const newItem = {
      task_id: taskId,
      scheduled_time: '',
      duration_minutes: task.estimated_minutes,
    };
    try {
      await savePlan({ ...plan, items: [...plan.items, newItem] });
      setShowAddTask(false);
      load();
    } catch (e) {
      setError(e.message);
    }
  };

  const handleActualTimeChange = async (taskId, hours) => {
    const minutes = Math.round(Number(hours) * 60);
    if (isNaN(minutes) || minutes < 0) return;
    // Optimistic local update
    setTaskMap(prev => ({
      ...prev,
      [taskId]: { ...prev[taskId], actual_minutes: minutes },
    }));
    try {
      await updateTask(taskId, { actual_minutes: minutes });
    } catch (e) {
      setError(e.message);
      load(); // revert on error
    }
  };

  const handleSplitTask = async (taskId) => {
    const names = splitNames.map(n => n.trim()).filter(Boolean);
    if (names.length === 0) return;
    const parent = taskMap[taskId];
    if (!parent) return;

    try {
      // Create child tasks inheriting parent properties
      const children = [];
      for (const name of names) {
        const child = await createTask({
          parent_id: taskId,
          title: name,
          priority: parent.priority,
          category: parent.category,
          status: parent.status,
          estimated_minutes: parent.estimated_minutes,
          due_date: parent.due_date || '',
        });
        children.push(child);
      }

      // Replace parent in daily plan with children
      if (plan) {
        const newItems = [];
        for (const item of plan.items) {
          if (item.task_id === taskId) {
            for (const child of children) {
              newItems.push({ task_id: child.id, scheduled_time: item.scheduled_time, duration_minutes: item.duration_minutes });
            }
          } else {
            newItems.push(item);
          }
        }
        await savePlan({ ...plan, items: newItems });
      }

      // Also replace in weekly plan if present
      const weeklyPlan = await getPlan('weekly', date);
      if (weeklyPlan && weeklyPlan.id) {
        const hasTask = weeklyPlan.items.some(it => it.task_id === taskId);
        if (hasTask) {
          const newWeeklyItems = [];
          for (const item of weeklyPlan.items) {
            if (item.task_id === taskId) {
              for (const child of children) {
                newWeeklyItems.push({ task_id: child.id, scheduled_time: '', duration_minutes: item.duration_minutes });
              }
            } else {
              newWeeklyItems.push(item);
            }
          }
          await savePlan({ ...weeklyPlan, items: newWeeklyItems });
        }
      }

      setSplittingTaskId(null);
      setSplitNames(['', '']);
      load();
    } catch (e) {
      setError(e.message);
    }
  };

  const handleRemoveFromWeek = async (taskId) => {
    try {
      const weeklyPlan = await getPlan('weekly', date);
      if (weeklyPlan && weeklyPlan.id) {
        const newItems = weeklyPlan.items.filter(it => it.task_id !== taskId);
        await savePlan({ ...weeklyPlan, items: newItems });
      }
      // Also remove from current daily plan
      if (plan) {
        const newItems = plan.items.filter(it => it.task_id !== taskId);
        await savePlan({ ...plan, items: newItems });
      }
      load();
    } catch (e) {
      setError(e.message);
    }
  };

  // ── Weekly Plan View ─────────────────────────────────────────────
  if (type === 'weekly') {
    const totalMinutes = plan ? plan.items.reduce((sum, it) => sum + it.duration_minutes, 0) : 0;
    const planItemIds = new Set(plan ? plan.items.map(it => it.task_id) : []);

    // Tasks available to add: leaf tasks not already in plan, not done
    const hasChildren = (id) => allTasks.some(t => t.parent_id === id);
    const availableTasks = allTasks.filter(
      t => !planItemIds.has(t.id) && t.status !== 'done' && !hasChildren(t.id)
    );

    const planTree = plan ? buildPlanTree(plan.items, taskMap) : [];

    // Map from item task_id to its flat index in plan.items
    const getItemIndex = (taskId) => plan.items.findIndex(it => it.task_id === taskId);

    return (
      <div>
        <div className="page-header">
          <h2>Weekly Plan</h2>
          <div style={{ display: 'flex', gap: 10, alignItems: 'center' }}>
            <input type="date" value={date} onChange={e => setDate(e.target.value)} />
            <button className="btn btn-primary" onClick={handleGenerateWeekly} disabled={loading}>
              Generate Weekly
            </button>
          </div>
        </div>
        <p style={{ color: '#636e72', marginBottom: 16 }}>
          Week of {monday} &mdash; {plan ? `${plan.items.length} tasks, ${formatDuration(totalMinutes)} total` : 'No plan yet'}
        </p>

        {error && <div style={{ color: '#d63031', marginBottom: 12 }}>{error}</div>}

        {loading ? (
          <div className="empty">Loading...</div>
        ) : !plan ? (
          <div className="empty">
            No weekly plan yet. Click "Generate Weekly" to create one from your tasks.
          </div>
        ) : (
          <>
            {/* Hierarchy view of weekly plan */}
            <div className="card">
              {planTree.map(group => {
                const groupMinutes = group.items.reduce((s, it) => s + it.duration_minutes, 0);
                const tree = buildNestedTree(group.items, group.rootId, taskMap);

                const getSubtotal = (n) => n.planItem ? n.planItem.duration_minutes : n.children.reduce((a, ch) => a + getSubtotal(ch), 0);

                const renderLeaf = (node, depth) => {
                  const task = node.task;
                  const idx = getItemIndex(node.id);
                  const isDone = task?.status === 'done';
                  return (
                    <div key={node.id}
                         className={`plan-group-item ${isDone ? 'plan-item-done' : ''}`}
                         style={{ paddingLeft: 12 + depth * 24 }}>
                      <div style={{ flex: 1, cursor: 'pointer' }} onClick={() => task && setSelectedTask(task)}>
                        <span style={isDone ? { textDecoration: 'line-through', color: '#b2bec3' } : {}}>
                          {task ? task.title : `Task #${node.id}`}
                        </span>
                        {isDone && <span className="status-badge status-done" style={{ marginLeft: 8 }}>done</span>}
                      </div>
                      <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                        <span style={{ color: '#636e72', fontSize: '0.8rem' }}>
                          Est: {formatDuration(node.planItem.duration_minutes)}
                        </span>
                        <span style={{ color: '#636e72', fontSize: '0.8rem' }}>Real:</span>
                        <input type="number" min="0" step="0.5"
                               value={task ? +(task.actual_minutes / 60).toFixed(1) : 0}
                               onChange={e => handleActualTimeChange(node.id, e.target.value)}
                               style={{ width: 55 }} />
                        <span style={{ color: '#636e72', fontSize: '0.8rem' }}>h</span>
                        <button className="btn btn-sm btn-danger"
                                onClick={() => handleRemoveItem(idx)}>Remove</button>
                      </div>
                    </div>
                  );
                };

                const renderNode = (node, depth) => {
                  // Leaf that is also the root (standalone task) — render directly
                  if (node.isLeaf && depth === 0) return renderLeaf(node, depth);
                  // Leaf child — wrap with parent-style label + controls underneath
                  if (node.isLeaf) {
                    return (
                      <div key={node.id}>
                        <div className="plan-tree-parent" style={{ paddingLeft: 12 + depth * 24 }}>
                          <span style={{ color: '#2d3436', fontWeight: 600, fontSize: '0.85rem', cursor: 'pointer' }}
                                onClick={() => node.task && setSelectedTask(node.task)}>
                            {node.task?.title || `Task #${node.id}`}
                          </span>
                          <span style={{ color: '#636e72', fontSize: '0.8rem', marginLeft: 8 }}>
                            ({formatDuration(node.planItem.duration_minutes)})
                          </span>
                        </div>
                        {renderLeaf(node, depth + 1)}
                      </div>
                    );
                  }
                  // Root node — header is already rendered, just render children
                  if (depth === 0) {
                    return node.children.map(child => renderNode(child, depth + 1));
                  }
                  // Intermediate parent node
                  return (
                    <div key={node.id}>
                      <div className="plan-tree-parent" style={{ paddingLeft: 12 + depth * 24 }}>
                        <span style={{ color: '#2d3436', fontWeight: 600, fontSize: '0.85rem' }}>
                          {node.task?.title || `Task #${node.id}`}
                        </span>
                        <span style={{ color: '#636e72', fontSize: '0.8rem', marginLeft: 8 }}>
                          ({formatDuration(getSubtotal(node))})
                        </span>
                      </div>
                      {node.children.map(child => renderNode(child, depth + 1))}
                    </div>
                  );
                };

                return (
                  <div key={group.rootId} className="plan-group">
                    <div className="plan-group-header">
                      <span className={`priority priority-${group.rootTask?.priority || 3}`} />
                      <strong>{group.rootTask?.title || `Task #${group.rootId}`}</strong>
                      <span style={{ color: '#636e72', fontSize: '0.85rem', marginLeft: 8 }}>
                        ({formatDuration(groupMinutes)})
                      </span>
                      {group.rootTask?.category && (
                        <span style={{ color: '#b2bec3', fontSize: '0.8rem', marginLeft: 8 }}>
                          {group.rootTask.category}
                        </span>
                      )}
                    </div>
                    <div className="plan-group-items">
                      {tree.isLeaf
                        ? renderNode(tree, 0)
                        : tree.children.map(child => renderNode(child, 1))
                      }
                    </div>
                  </div>
                );
              })}
            </div>

            {/* Add task button */}
            <div style={{ margin: '16px 0' }}>
              {showAddTask ? (
                <div className="card" style={{ padding: 16 }}>
                  <label style={{ fontWeight: 500, marginBottom: 8, display: 'block' }}>Add task to weekly plan:</label>
                  {availableTasks.length === 0 ? (
                    <p style={{ color: '#b2bec3' }}>No available tasks to add.</p>
                  ) : (
                    <select
                      defaultValue=""
                      onChange={e => { if (e.target.value) handleAddTask(Number(e.target.value)); }}
                      style={{ width: '100%', padding: '8px 10px', borderRadius: 6, border: '1px solid #dfe6e9' }}
                    >
                      <option value="" disabled>Select a task...</option>
                      {availableTasks.map(t => (
                        <option key={t.id} value={t.id}>
                          {t.parent_id ? getTaskPath(t.id, taskMap) : t.title}
                          {' '}({formatDuration(t.estimated_minutes)})
                        </option>
                      ))}
                    </select>
                  )}
                  <button className="btn" style={{ marginTop: 8 }} onClick={() => setShowAddTask(false)}>Cancel</button>
                </div>
              ) : (
                <button className="btn" onClick={() => setShowAddTask(true)}>+ Add Task to Plan</button>
              )}
            </div>

            <div style={{ margin: '24px 0' }}>
              <button className="btn btn-primary" onClick={handleGenerateDaily} disabled={loading}>
                Generate Daily Plans from Weekly
              </button>
            </div>

            {dailyPlans.some(dp => dp.plan) && (
              <DailyPlansDragGrid
                dailyPlans={dailyPlans}
                taskMap={taskMap}
                onPlansUpdated={load}
                onTaskClick={setSelectedTask}
              />
            )}
          </>
        )}
        <TaskDetailModal task={selectedTask} taskMap={taskMap} onClose={() => setSelectedTask(null)} />
      </div>
    );
  }

  // ── Daily Plan View ────────────────────────────────────────────────
  // Show review modal if there are unreviewed past plans
  if (unreviewedPlans.length > 0) {
    return (
      <ReviewModal
        plans={unreviewedPlans}
        taskMap={taskMap}
        onComplete={handleReviewComplete}
        onSkip={handleReviewSkip}
      />
    );
  }

  return (
    <div>
      <div className="page-header">
        <h2>Daily Plan</h2>
        <div style={{ display: 'flex', gap: 10, alignItems: 'center' }}>
          <input type="date" value={date} onChange={e => setDate(e.target.value)} />
          <button className="btn btn-primary" onClick={handleGenerateDaily} disabled={loading}>
            Refresh from Weekly
          </button>
        </div>
      </div>

      {error && <div style={{ color: '#d63031', marginBottom: 12 }}>{error}</div>}

      {dailyWarning && (
        <div className="warning-banner">
          {dailyWarning}
        </div>
      )}

      {loading ? (
        <div className="empty">Loading...</div>
      ) : !plan ? (
        <div className="empty">
          No daily plan for this date. Generate it from the Weekly Plan page.
        </div>
      ) : plan.items.length === 0 ? (
        <div className="empty">No tasks scheduled for this day.</div>
      ) : (
        <div>
          {buildPlanTree(plan.items, taskMap).map(group => {
            const groupMinutes = group.items.reduce((s, it) => s + it.duration_minutes, 0);
            const groupDone = group.items.every(it => taskMap[it.task_id]?.status === 'done');
            const tree = buildNestedTree(group.items, group.rootId, taskMap);
            const getSubtotal = (n) => n.planItem ? n.planItem.duration_minutes : n.children.reduce((a, ch) => a + getSubtotal(ch), 0);

            const renderDailyLeaf = (node, depth) => {
              const task = node.task;
              const isDone = task?.status === 'done';
              const isSplitting = splittingTaskId === node.id;
              return (
                <div key={`leaf-${node.id}`}>
                  <div className={`plan-group-item ${isDone ? 'plan-item-done' : ''}`}
                       style={{ paddingLeft: 12 + depth * 24 }}>
                    <div style={{ flex: 1, cursor: 'pointer' }} onClick={() => task && setSelectedTask(task)}>
                      <span style={isDone ? { textDecoration: 'line-through', color: '#b2bec3' } : {}}>
                        {task ? task.title : `Task #${node.id}`}
                      </span>
                    </div>
                    <div style={{ display: 'flex', alignItems: 'center', gap: 4, marginRight: 6 }}>
                      <span style={{ color: '#636e72', fontSize: '0.8rem' }}>Est: {formatDuration(node.planItem.duration_minutes)}</span>
                      <span style={{ color: '#636e72', fontSize: '0.8rem' }}>Real:</span>
                      <input type="number" min="0" step="0.5"
                             value={task ? +(task.actual_minutes / 60).toFixed(1) : 0}
                             onChange={e => handleActualTimeChange(node.id, e.target.value)}
                             style={{ width: 55 }} />
                      <span style={{ color: '#636e72', fontSize: '0.8rem' }}>h</span>
                    </div>
                    <div style={{ display: 'flex', gap: 4 }}>
                      <button
                        className={`btn btn-sm ${isDone ? '' : 'btn-primary'}`}
                        onClick={async () => {
                          await updateTask(node.id, { status: isDone ? 'todo' : 'done' });
                          load();
                        }}
                      >
                        {isDone ? 'Undo' : 'Done'}
                      </button>
                      <button className="btn btn-sm" title="Split into subtasks"
                        onClick={() => {
                          if (isSplitting) { setSplittingTaskId(null); setSplitNames(['', '']); }
                          else { setSplittingTaskId(node.id); setSplitNames(['', '']); }
                        }}
                      >
                        Split
                      </button>
                      <button className="btn btn-sm btn-danger" title="Remove from weekly and daily plan"
                        onClick={() => handleRemoveFromWeek(node.id)}
                      >
                        Remove
                      </button>
                    </div>
                  </div>
                  {isSplitting && (
                    <div style={{ padding: '8px 12px', background: '#f8f9fa', borderRadius: 6, margin: '4px 0 8px', marginLeft: 12 + depth * 24 }}>
                      <div style={{ fontSize: '0.8rem', color: '#636e72', marginBottom: 6 }}>
                        Split "{task?.title}" into subtasks:
                      </div>
                      {splitNames.map((name, idx) => (
                        <input key={idx} type="text" placeholder={`Subtask ${idx + 1}`} value={name}
                          onChange={e => { const next = [...splitNames]; next[idx] = e.target.value; setSplitNames(next); }}
                          style={{ width: '100%', padding: '4px 8px', marginBottom: 4, borderRadius: 4, border: '1px solid #dfe6e9', fontSize: '0.85rem' }}
                          autoFocus={idx === 0} />
                      ))}
                      <div style={{ display: 'flex', gap: 6, marginTop: 4 }}>
                        <button className="btn btn-sm" onClick={() => setSplitNames([...splitNames, ''])}>+ Add</button>
                        <button className="btn btn-sm btn-primary" disabled={splitNames.every(n => !n.trim())}
                          onClick={() => handleSplitTask(node.id)}>Split</button>
                        <button className="btn btn-sm" onClick={() => { setSplittingTaskId(null); setSplitNames(['', '']); }}>Cancel</button>
                      </div>
                    </div>
                  )}
                </div>
              );
            };

            const renderDailyNode = (node, depth) => {
              if (node.isLeaf && depth === 0) return renderDailyLeaf(node, depth);
              if (node.isLeaf) {
                return (
                  <div key={node.id}>
                    <div className="plan-tree-parent" style={{ paddingLeft: 12 + depth * 24 }}>
                      <span style={{ color: '#2d3436', fontWeight: 600, fontSize: '0.85rem', cursor: 'pointer' }}
                            onClick={() => node.task && setSelectedTask(node.task)}>
                        {node.task?.title || `Task #${node.id}`}
                      </span>
                      <span style={{ color: '#636e72', fontSize: '0.8rem', marginLeft: 8 }}>
                        ({formatDuration(node.planItem.duration_minutes)})
                      </span>
                    </div>
                    {renderDailyLeaf(node, depth + 1)}
                  </div>
                );
              }
              if (depth === 0) {
                return node.children.map(child => renderDailyNode(child, depth + 1));
              }
              return (
                <div key={node.id}>
                  <div className="plan-tree-parent" style={{ paddingLeft: 12 + depth * 24 }}>
                    <span style={{ color: '#2d3436', fontWeight: 600, fontSize: '0.85rem' }}>
                      {node.task?.title || `Task #${node.id}`}
                    </span>
                    <span style={{ color: '#636e72', fontSize: '0.8rem', marginLeft: 8 }}>
                      ({formatDuration(getSubtotal(node))})
                    </span>
                  </div>
                  {node.children.map(child => renderDailyNode(child, depth + 1))}
                </div>
              );
            };

            return (
              <div key={group.rootId} className="plan-group">
                <div className="plan-group-header">
                  <span className={`priority priority-${group.rootTask?.priority || 3}`} />
                  <strong>{group.rootTask?.title || `Task #${group.rootId}`}</strong>
                  <span style={{ color: '#636e72', fontSize: '0.85rem', marginLeft: 8 }}>
                    ({formatDuration(groupMinutes)})
                  </span>
                  {group.rootTask?.category && (
                    <span style={{ color: '#b2bec3', fontSize: '0.8rem', marginLeft: 8 }}>
                      {group.rootTask.category}
                    </span>
                  )}
                  {groupDone && <span className="status-badge status-done" style={{ marginLeft: 8 }}>done</span>}
                </div>
                <div className="plan-group-items">
                  {tree.isLeaf
                    ? renderDailyNode(tree, 0)
                    : tree.children.map(child => renderDailyNode(child, 1))
                  }
                </div>
              </div>
            );
          })}
        </div>
      )}
      <TaskDetailModal task={selectedTask} taskMap={taskMap} onClose={() => setSelectedTask(null)} />
    </div>
  );
}
