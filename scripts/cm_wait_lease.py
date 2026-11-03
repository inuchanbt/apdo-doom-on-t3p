"""Renew only an unused, powered-off wait. Never retry a transmitted key.

The helper's own once-per-boot latch remains the final output gate. Renewal
needs an explicit DISARM acknowledgement before a new ARM. A power event,
used state, error, or missing acknowledgement disables renewal.
"""
ARM=b'ARM_STABLE_POWER_ONCE\n'
DISARM=b'DISARM\n'
ACTIVITY={'power_candidate','power_bounce','power_edge','key_start_timing',
          'key_finished','host_started','device_mounted','arm_expired'}

class WaitLease:
    def __init__(self,interval=240):
        self.interval=interval
        self.phase='initial'
        self.next_renew=None
        self.pending_deadline=None
        self.disabled=False
        self.renewals=0

    def stop(self):
        self.disabled=True
        self.pending_deadline=None

    def read_deadline(self,overall):
        return min(overall,self.pending_deadline) if self.pending_deadline is not None else overall

    def feed(self,item,now):
        if self.disabled: return None
        if self.pending_deadline is not None and now>=self.pending_deadline:
            self.stop(); raise TimeoutError('Wait-renewal acknowledgement missing; no retry')
        ev=item.get('event')
        if ev in ACTIVITY:
            self.stop(); return None
        if ev=='armed':
            if self.phase=='await_arm': self.renewals+=1
            self.phase='active'; self.next_renew=now+self.interval
            self.pending_deadline=None
            return None
        if ev!='status': return None
        # Caller validates the exact helper profile before this method.
        if any(item.get(k) is not False for k in ('used','host_requested','fault','bias_active')) or item.get('frames')!=0:
            self.stop(); return None
        if item['adc_raw']>310:
            if self.phase=='await_disarm' and item['armed']:
                return None  # Await either the disarm acknowledgement or a key event.
            was_pending=self.phase=='await_disarm'
            self.stop()
            if was_pending: raise ValueError('Power changed during wait renewal; cancelled without retry')
            return None
        if self.phase=='await_disarm':
            if item['armed']: return None  # Older queued status may precede acknowledgement.
            if not all(item.get(k) is True for k in ('adc_on_checked','adc_off_checked','bias_checked')):
                self.stop(); raise ValueError('Wait renewal lost preflight checks; no retry')
            self.phase='await_arm'; self.pending_deadline=now+3
            return ARM
        if self.phase=='active':
            if not item['armed']:
                self.stop(); raise ValueError('Wait unexpectedly ended; no automatic rearm')
            if now>=self.next_renew:
                self.phase='await_disarm'; self.pending_deadline=now+3
                return DISARM
        return None
