export default function StageBadge({ task, size = 'sm' }) {
  if (!task) return null;
  const stages = Array.isArray(task.stages) ? task.stages : [];
  if (stages.length <= 1) return null;
  const current = stages[task.current_stage || 0];
  if (!current) return null;
  const fontSize = size === 'xs' ? '0.65rem' : '0.7rem';
  const padding = size === 'xs' ? '1px 5px' : '1px 6px';
  return (
    <span
      title={stages.map((s, i) => `${i + 1}. ${s.name}${s.completed_at ? ' \u2713' : ''}`).join('\n')}
      style={{
        fontSize,
        color: '#0984e3',
        background: '#dfe6e9',
        padding,
        borderRadius: 4,
        fontWeight: 500,
        marginLeft: 6,
        whiteSpace: 'nowrap',
      }}
    >
      {current.name} {(task.current_stage || 0) + 1}/{stages.length}
    </span>
  );
}
