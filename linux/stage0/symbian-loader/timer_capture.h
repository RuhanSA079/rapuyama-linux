// RM-609 timer layouts checked against ekern, not the older sdk structs
const TInt KTimerRows=512, KTimerSamples=36;
const TInt KTimerFrameWords=16+KTimerRows*32;
const TInt KTimerCaptureBytes=(8+KTimerSamples*KTimerFrameWords)*4;

static TBool TimerRead(TUint32 a, TAny* out, TInt n){
	if((a&3) || a<0xc8000000u || a>=0xd0000000u || (TUint32)n>0xd0000000u-a){return EFalse;}
	return Kern::SafeRead((const TAny*)a, out, n)==NULL;
}

static void TimerName(TUint32 obj, char* out, TInt cap){
	TUint32 name=0, h[3];
	if(!TimerRead(obj+0x14, &name, 4) || !TimerRead(name, h, sizeof(h))){return;}
	TUint32 type=h[0]>>28, len=h[0]&0xfffffff, src=0;
	if(type==0){src=name+4;}
	else if(type==1){src=h[1];}
	else if(type==2){src=h[2];}
	else if(type==3){src=name+8;}
	else{return;}
	if(len>(TUint32)cap-1){len=cap-1;}
	// SafeRead accepts only aligned words, descriptor bytes need not be aligned
	for(TUint32 i=0; i<len; ++i){
		TUint32 w=0, a=(src+i)&~3u;
		if(!TimerRead(a, &w, 4)){out[0]=0; return;}
		out[i]=(char)(w>>(((src+i)&3)*8));
	}
}

static void TimerWalk(TUint32 head, TUint32 kind, TUint32* frame){
	TUint32 node=0, prev=head;
	if(!TimerRead(head, &node, 4)){frame[4]|=1; return;}
	while(node!=head){
		if((TUint32)(FastCounter()-frame[1])>164){frame[4]|=8; return;}
		if(frame[3]>=KTimerRows){frame[4]|=2; return;}
		TUint32* row=frame+16+frame[3]*32;
		row[0]=kind;
		row[1]=node;
		if(!TimerRead(node, row+2, 56)){frame[4]|=1; return;}
		if(row[3]!=prev){frame[4]|=4; return;}
		++frame[3];
		// DTimer callbacks use the TTimer pointer; its owner is at -0x14
		TUint32 fn=row[7], arg=row[6];
		if(kind==1 && fn==0){
			TimerRead(arg+0x14, row+16, 4);
			TimerRead(arg+0x10, row+17, 4);
		}
		if(fn==0x8009bf3c || fn==0x8009bf84 || fn==0x8009c0fc){
			TUint32 owner=0, process=0;
			if(TimerRead(arg-0x14, &owner, 4)){
				row[16]=owner;
				TimerName(owner, (char*)(row+17), 28);
				if(TimerRead(owner+0xc, &process, 4)){TimerName(process, (char*)(row+24), 32);}
			}
		}
		prev=node;
		node=row[2];
	}
}

static void TimerSnapshot(TUint32* frame){
	// unlocked reads can race with timer changes; broken links are flagged
	// never fault in a page or wait for a mutex with interrupts masked
	frame[1]=FastCounter();
	frame[0]=NKern::TickCount();
	const TUint32 nq=0xc80006d0u;
	TUint32 tq=0, sq=0, q[14];
	if(TimerRead(0xc8000604u, &tq, 4) && TimerRead(tq, q, sizeof(q))){
		frame[5]=tq;
		frame[8]=q[12];
		frame[9]=q[11];
		frame[10]=q[6];
		frame[11]=q[13];
		frame[12]=q[4];
		frame[13]=q[5];
		TimerWalk(tq, 2, frame);
	}else{frame[4]|=1;}
	if(TimerRead(0xc8000608u, &sq, 4)){
		frame[6]=sq;
		TimerWalk(sq, 3, frame);
	}else{frame[4]|=1;}
	for(TUint32 off=0; off<0x200; off+=8){TimerWalk(nq+off, 1, frame);}
	TimerWalk(nq+0x210, 1, frame);
	TimerWalk(nq+0x218, 1, frame);
	TimerWalk(nq+0x220, 1, frame);
	frame[2]=FastCounter();
}

static TInt CaptureTimers(TAny* user, TUint32 target){
	if(NKern::TickPeriod()!=1000){return KErrNotSupported;}
	TUint32 code[2];
	if(Kern::SafeRead((const TAny*)0x80064280u, code, sizeof(code)) ||
		code[0]!=0xe51f0980u || code[1]!=0xe12fff1eu){
		return KErrNotSupported;
	}
	TUint32* frame=(TUint32*)Kern::AllocZ(KTimerFrameWords*4);
	if(!frame){return KErrNoMemory;}
	TUint32 header[8]={0x31524d54, 1, target, KTimerFrameWords,
		KTimerRows, KTimerSamples, (TUint32)NKern::TickPeriod(), 0};
	TPtrC8 h((const TUint8*)header, sizeof(header));
	TInt r=Kern::ThreadDesWrite(&Kern::CurrentThread(), user, h, 0);
	for(TInt i=0; i<KTimerSamples; ++i){
		if(r!=KErrNone){break;}
		TUint32 at=target-3000+i*100;
		TInt wait=(TInt)(at-NKern::TickCount());
		if(wait>0){NKern::Sleep(wait);}
		for(TInt j=0; j<KTimerFrameWords; ++j){frame[j]=0;}
		TimerSnapshot(frame);
		TPtrC8 sample((const TUint8*)frame, KTimerFrameWords*4);
		r=Kern::ThreadDesWrite(&Kern::CurrentThread(), user, sample,
			(8+i*KTimerFrameWords)*4);
	}
	Kern::Free(frame);
	return r;
}