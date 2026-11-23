-- LUMPI cloud storage: the single source of truth.
-- Every event from the nest and the AI is one row, stored once by its event_id.
-- Same rules as check_event() in laptop/dashboard_server.py; the database
-- repeats them so a bug in the web code can never store a bad row.
--
-- Safe to run twice (if not exists / drop ... if exists), because it may be
-- pasted into the SQL editor first and applied again by the GitHub integration.

create table if not exists public.events (
    event_id    text primary key check (length(event_id) between 3 and 95),
    node_id     integer not null check (node_id between 0 and 3),
    event_type  text not null check (event_type in ('room', 'start', 'status', 'score', 'help', 'noise')),
    recorded_at bigint not null check (recorded_at > 1700000000),  -- when it happened (nest NTP clock)
    activity    text check (activity in ('idle', 'book_on', 'recording', 'waiting_ai', 'feedback', 'quiet_study', 'stopped')),
    data        jsonb not null check (jsonb_typeof(data) = 'object'),
    session     bigint,  -- score only: recorded_at when that reading session began
    received_at double precision not null default extract(epoch from now())  -- when the cloud got it
);

create index if not exists events_by_received on public.events (received_at);
create index if not exists events_by_type_time on public.events (event_type, recorded_at);
create index if not exists events_by_node_time on public.events (node_id, recorded_at);

-- Messages that failed the checks, for the developer page (/debug/).
create table if not exists public.rejects (
    id          bigint generated always as identity primary key,
    received_at double precision not null default extract(epoch from now()),
    source      text,
    reason      text not null,
    raw         text,
    event_type  text
);

create index if not exists rejects_by_received on public.rejects (received_at);

-- Row Level Security on, with NO policies: the publishable key can do nothing.
-- Only the server (Vercel function, secret key) reads and writes.
alter table public.events enable row level security;
alter table public.rejects enable row level security;

revoke all on public.events, public.rejects from anon, authenticated;
grant select, insert on public.events, public.rejects to service_role;
