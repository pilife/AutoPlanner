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

        <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
          {plan.items.map((item, i) => {
            const task = taskMap[item.task_id];
            const review = reviews[i];
            return (
              <div key={item.task_id} className="review-item">
                <div style={{ flex: 1 }}>
                  <strong>{task ? task.title : `Task #${item.task_id}`}</strong>
                  {task && task.parent_id > 0 && (
                    <div style={{ fontSize: '0.75rem', color: '#999' }}>
                      {getTaskPath(item.task_id, taskMap)}
                    </div>
                  )}
                  <div style={{ fontSize: '0.8rem', color: '#636e72' }}>
                    Planned: {formatDuration(item.duration_minutes)}
                  </div>
                </div>
                <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                  <select
                    value={review.status}
                    onChange={e => updateReview(i, 'status', e.target.value)}
                    style={{ padding: '4px 8px', borderRadius: 6, border: '1px solid #dfe6e9' }}
                  >
                    <option value="done">Done</option>
                    <option value="todo">Not done</option>
                    <option value="in_progress">Partial</option>
                  </select>
                  <div>
                    <input
                      type="number" min="0" step="5"
                      value={review.actual_minutes}
                      onChange={e => updateReview(i, 'actual_minutes', Number(e.target.value))}
                      style={{ width: 60, padding: '4px 6px', borderRadius: 6, border: '1px solid #dfe6e9' }}
                    />
                    <span style={{ fontSize: '0.8rem', color: '#636e72', marginLeft: 4 }}>min</span>
                  </div>
                </div>
              </div>
            );
          })}
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
