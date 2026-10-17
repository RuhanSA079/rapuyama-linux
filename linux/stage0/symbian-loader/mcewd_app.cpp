// e6mcewd: disable the modem (CMT) watchdog from LIVE Symbian, once, before the
// Linux handoff. Theory (Opus 5.5): the external ~50-210 s warm reset is the
// modem supervising the APE; if we tell the modem MCE server to turn its
// watchdog off, the setting persists after Symbian dies and Linux survives.
//
// All values from the EPL-1.0 modem adaptation sources (owner's own phone):
//   ISC channel = EIscNokiaReserved8 = 0x19 (the channel the SSM adaptation
//     opens to talk to PN_MODEM_MCE, ssm_emergencycallrf_adaptation.cpp:159)
//   ISI msg (phonetisi.h offsets): [3]=resource PN_MODEM_MCE 0xC2,
//     [8]=transaction, [9]=message MCE_WATCHDOG_CONTROL_REQ 0x0C,
//     [10]=ACTION MCE_DISABLE 0x00 (mceisi.h). Reply MCE_WATCHDOG_CONTROL_RESP
//     0x0D, status byte [10] MCE_OK 0x00.
// Routing: [1] receiver device MUST be PN_DEV_MODEM 0x60 like the SSM does
// (ssm_emergencycallrf_adaptation.cpp:461). With [1]=0 (PN_DEV_OWN) the IAD
// routes it locally and answers COMM_ISA_ENTITY_NOT_REACHABLE_RESP (f0/14),
// which is what run 2 got. [0,2,7] are stamped by DRouter::SetSenderInfo.
//
// WORKFLOW: run e6mcewd in Symbian -> read E:\mcewd.log. If it logs
// "RESP ok status=0" the MCE channel works; then launch Linux (e6boot) and see
// if the reset is gone. "RESP ok" + survival >> 210 s = fixed. No RESP = the
// channel/routing is wrong (not that the theory is dead - see the log).
// Caveat: header is wgmodem25; this watchdog may guard the modem itself, not
// the APE supervision.

#include <e32std.h>
#include <e32base.h>
#include <f32file.h>
#include <iscapi.h>

_LIT(KLog, "E:\\mcewd.log");

static RFs gFs;

static void L(const TDesC8 &m)
{
	RFile f;
	TInt r = f.Open(gFs, KLog, EFileWrite);
	if (r == KErrNotFound)
		r = f.Create(gFs, KLog, EFileWrite);
	if (r != KErrNone)
		return;
	TInt p = 0;
	f.Seek(ESeekEnd, p);
	f.Write(m);
	f.Flush();
	f.Close();
}

static void Lf(TRefByValue<const TDesC8> fmt, ...)
{
	VA_LIST a;
	VA_START(a, fmt);
	TBuf8<128> b;
	b.FormatList(fmt, a);
	VA_END(a);
	L(b);
}

// wait for aStatus, but give up after aMs milliseconds
static TInt WaitTmo(TRequestStatus &st, TInt aMs)
{
	RTimer t;
	if (t.CreateLocal() != KErrNone)
		return KErrGeneral;
	TRequestStatus tmo;
	t.After(tmo, aMs * 1000);
	User::WaitForRequest(st, tmo);
	if (st == KRequestPending) {		/* timed out */
		t.Cancel();
		User::WaitForRequest(tmo);
		t.Close();
		return KErrTimedOut;
	}
	t.Cancel();
	User::WaitForRequest(tmo);
	t.Close();
	return KErrNone;
}

GLDEF_C TInt E32Main()
{
	if (gFs.Connect() != KErrNone)
		return KErrGeneral;
	L(_L8("e6mcewd: start\n"));

	RIscApi isc;
	TRequestStatus st;

	isc.InitializeModemInterface(st);
	User::WaitForRequest(st);
	Lf(_L8("init: %d\n"), st.Int());

	isc.Open(0x19 /*EIscNokiaReserved8*/, st);
	User::WaitForRequest(st);
	Lf(_L8("open(0x19): %d\n"), st.Int());
	if (st.Int() != KErrNone) {
		isc.Close();
		gFs.Close();
		return st.Int();
	}

	// MCE_WATCHDOG_CONTROL_REQ, ACTION = MCE_DISABLE, 12 bytes
	TBuf8<12> msg;
	msg.FillZ(12);
	msg[1] = 0x60;		/* PN_DEV_MODEM, 0 = local -> not reachable */
	msg[3] = 0xC2;		/* PN_MODEM_MCE */
	/* ISI length = total-6 = 6, stored [4]=low [5]=high (modem-link-spec); the
	 * first try had these swapped -> stack read len 0x0600 -> Send KErrUnderflow(-10) */
	msg[4] = 0x06;
	msg[5] = 0x00;
	msg[8] = 0x01;		/* transaction id */
	msg[9] = 0x0C;		/* MCE_WATCHDOG_CONTROL_REQ */
	msg[10] = 0x00;		/* MCE_DISABLE */

	TInt sr = isc.Send(msg);
	Lf(_L8("send: %d\n"), sr);

	if (sr == KErrNone) {
		TBuf8<64> rx;
		TUint16 need = 0;
		isc.Receive(st, rx, need);
		TInt wr = WaitTmo(st, 5000);
		if (wr == KErrTimedOut) {
			isc.ReceiveCancel();
			User::WaitForRequest(st);
			L(_L8("resp: TIMEOUT (no reply in 5 s)\n"));
		} else if (st.Int() == KErrNone && rx.Length() >= 11) {
			Lf(_L8("resp: res=%02x msg=%02x status=%02x len=%d need=%d\n"),
			   rx[3], rx[9], rx[10], rx.Length(), need);
			TBuf8<208> hx;
			hx.Append(_L8("resp bytes:"));
			for (TInt i = 0; i < rx.Length(); i++)
				hx.AppendFormat(_L8(" %02x"), rx[i]);
			hx.Append('\n');
			L(hx);
			if (rx[9] == 0x0D && rx[10] == 0x00)
				L(_L8("RESP ok status=0 -> MCE watchdog DISABLE accepted\n"));
			else
				L(_L8("RESP came but not OK (see bytes above)\n"));
		} else {
			Lf(_L8("resp: err=%d len=%d\n"), st.Int(), rx.Length());
		}
	}

	isc.Close();
	L(_L8("e6mcewd: done\n"));
	gFs.Close();
	return KErrNone;
}