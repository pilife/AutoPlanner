import { useState } from 'react';
import { formatDuration, getTaskPath } from '../helpers';
import { reviewPlan } from '../api';

export default function ReviewModal({ plans, taskMap, onComplete, onSkip }) {
  const [currentPlanIdx, setCurrentPlanIdx] = useState(0);
  const [submitting, setSubmitting] = useState(false);

  const plan = plans[currentPlanIdx];

  const initReviews = (p) =>
    p.items.map(item => {
      const task = taskMap[item.task_id];
      return {
        task_id: item.task_id,
        status: task?.status === 'done' ? 'done' : 'todo',
        actual_minutes: item.duration_minutes,
      };
    });

  const [reviews, setReviews] = useState(() => initReviews(plan));

  const updateReview = (index, field, value) => {
    setReviews(prev => {
      const next = [...prev];
      next[index] = { ...next[index], [field]: value };
      return next;
    });
  };

  const handleSubmit = async () => {
    setSubmitting(true);
    try {
      await reviewPlan({
        plan_id: plan.id,
        plan_date: plan.date,
        tasks: reviews,
      });

      if (currentPlanIdx < plans.length - 1) {
        // Advance to next plan
        const nextIdx = currentPlanIdx + 1;
        const nextPlan = plans[nextIdx];
        setCurrentPlanIdx(nextIdx);
        setReviews(initReviews(nextPlan));
      } else {
        // All plans reviewed — notify parent to reload
        onComplete();
      }
    } catch (e) {
      alert('Review failed: ' + e.message);
    }
    setSubmitting(false);
  };

  if (!plan || plan.items.length === 0) {
    // Auto-submit empty plans
    reviewPlan({ plan_id: plan.id, plan_date: plan.date, tasks: [] }).then(() => {
      if (currentPlanIdx < plans.length - 1) {
        const nextIdx = currentPlanIdx + 1;
        setCurrentPlanIdx(nextIdx);
        setReviews(initReviews(plans[nextIdx]));
      } else {
        onComplete();
      }
    });
    return <div className="modal-overlay"><div className="modal"><p>Processing...</p></div></div>;
  }

  return (
    <div className="modal-overlay">
      <div className="modal" style={{ width: 560 }}>
        <h3>Review: {plan.date}</h3>
        <p style={{ color: '#636e72', marginBottom: 16, fontSize: '0.9rem' }}>
          Confirm each task's status and actual time spent.
          {plans.length > 1 && ` (${currentPlanIdx + 1} of ${plans.length} days to review)`}
        </p>

        <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
          {(() => {
            // Group items by root ancestor for hierarchy
            const getRootId = (taskId) => {
              let cur = taskMap[taskId];
              while (cur && cur.parent_id && taskMap[cur.parent_id]) cur = taskMap[cur.parent_id];
              return cur ? cur.id : taskId;
            };
            const groups = {};
            const rootOrder = [];
            plan.items.forEach((item, i) => {
              const rootId = getRootId(item.task_id);
              if (!groups[rootId]) { groups[rootId] = []; rootOrder.push(rootId); }
              groups[rootId].push({ item, index: i });
            });

            return rootOrder.map(rootId => {
              const rootTask = taskMap[rootId];
              const entries = groups[rootId];
              return (
                <div key={rootId} style={{ marginBottom: 8 }}>
                  <div style={{ fontSize: '0.8rem', fontWeight: 600, color: '#2d3436', padding: '6px 0', borderBottom: '2px solid #0984e3', marginBottom: 4 }}>
                    {rootTask?.title || `Task #${rootId}`}
                  </div>
                  {entries.map(({ item, index }) => {
                    const task = taskMap[item.task_id];
                    const review = reviews[index];
                    const isRoot = item.task_id === rootId;
                    // Build parent chain (excluding root, which is the group header)
                    const parentChain = [];
                    if (!isRoot) {
                      let cur = taskMap[item.task_id];
                      while (cur && cur.id !== rootId) {
                        parentChain.unshift(cur.title);
                        cur = cur.parent_id ? taskMap[cur.parent_id] : null;
                      }
                    }
                    return (
                      <div key={item.task_id} className="review-item" style={{ paddingLeft: isRoot ? 0 : 12 }}>
                        <div style={{ flex: 1 }}>
                          {parentChain.length > 1 && (
                            <div style={{ fontSize: '0.75rem', color: '#636e72' }}>
                              {parentChain.slice(0, -1).join(' > ')}
                            </div>
                          )}
                          <strong>{task ? task.title : `Task #${item.task_id}`}</strong>
                          <div style={{ fontSize: '0.8rem', color: '#636e72' }}>
                            Planned: {formatDuration(item.duration_minutes)}
                          </div>
                        </div>
                        <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                          <select
                            value={review.status}
                            onChange={e => updateReview(index, 'status', e.target.value)}
                            style={{ padding: '4px 8px', borderRadius: 6, border: '1px solid #dfe6e9' }}
                          >
                            <option value="done">Done</option>
                            <option value="todo">Not done</option>
                            <option value="in_progress">Partial</option>
                          </select>
                          <div>
                            <input
                              type="number" min="0" step="0.5"
                              value={+(review.actual_minutes / 60).toFixed(1)}
                              onChange={e => updateReview(index, 'actual_minutes', Math.round(Number(e.target.value) * 60))}
                              style={{ width: 55, padding: '4px 6px', borderRadius: 6, border: '1px solid #dfe6e9' }}
                            />
                            <span style={{ fontSize: '0.8rem', color: '#636e72', marginLeft: 4 }}>h</span>
                          </div>
                        </div>
                      </div>
                    );
                  })}
                </div>
              );
            });
          })()}
        </div>

        <div className="modal-actions">
          <button className="btn" onClick={onSkip}>Skip</button>
          <button className="btn btn-primary" onClick={handleSubmit} disabled={submitting}>
            {currentPlanIdx < plans.length - 1 ? 'Submit & Next' : 'Submit'}
          </button>
        </div>
      </div>
    </div>
  );
}
