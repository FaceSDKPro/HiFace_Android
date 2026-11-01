package com.hiface.sdk.demo.search;

import com.hiface.sdk.search.FaceSearchFace;
import com.hiface.sdk.search.FaceSearchFrame;
import com.hiface.sdk.search.FaceSearchResult;
import org.junit.Test;
import java.util.Arrays;
import static org.junit.Assert.*;

public final class FaceSearchOverlayStateTest {
    @Test public void resultCardPreservesFullIdZeroScoreAndClearsOldIdentity() {
        FaceSearchOverlayState state = newState();
        String id = new String(new char[900]).replace('\0', 'X') + "-END";
        FaceSearchFrame matched = frame(1, face(20, .98f, id));
        state.accept(matched, 0);
        assertEquals("", state.resultText());
        state.complete(new FaceSearchResult(FaceSearchResult.EndReason.MATCH_FOUND, 0f, matched), 100);
        assertTrue(state.resultText().contains("Face ID: " + id));
        assertTrue(state.resultText().contains("0.000"));
        assertEquals(Float.valueOf(0f), state.livenessScore());
        show(state, frame(2), 200);
        assertEquals("", state.resultText());
        assertNull(state.livenessScore());
        FaceSearchFrame low = frame(3, face(20, .4f, null));
        show(state, low, 300);
        state.complete(new FaceSearchResult(FaceSearchResult.EndReason.TIMEOUT, null, low), 400);
        assertFalse(state.resultText().isEmpty());
        assertFalse(state.resultText().contains("Face ID:"));
        state.expire(1400);
        assertEquals("", state.resultText());
        FaceSearchFrame spoof = frame(4, face(20, .98f, id));
        state.accept(spoof, 2000);
        state.complete(new FaceSearchResult(FaceSearchResult.EndReason.SPOOF_DETECTED, .2f, spoof), 2100);
        assertFalse(state.resultText().contains(id));
    }
    @Test public void earlyExpiryCheckDoesNotClearAFrameWhoseDeadlineWasExtended() {
        FaceSearchOverlayState state = newState();
        FaceSearchOverlayExpirySchedule schedule = new FaceSearchOverlayExpirySchedule();
        state.accept(frame(1, face(20, null, null)), 0);
        assertTrue(schedule.update(state.expiresAt()));
        state.accept(frame(2, face(22, null, null)), 1000);
        assertFalse(schedule.update(state.expiresAt()));
        assertFalse(state.expire(1200));
        assertTrue(state.hasVisibleFaces());
        schedule.reset();
        assertTrue(schedule.update(state.expiresAt()));
        assertTrue(state.expire(2200));
        assertFalse(state.hasVisibleFaces());
        assertFalse(state.expire(2201));
    }

    @Test public void idleEmptyFramesAdvanceIdentityWithoutRequestingRedraws() {
        FaceSearchOverlayState state = newState();
        for (int i = 1; i <= 10_000; i++) {
            assertFalse(state.accept(frame(i), i * 200L));
            assertEquals(i, state.newestFrameId());
            assertFalse(state.hasVisibleFaces());
            assertEquals(0, state.expiresAt());
        }
        assertFalse(state.accept(frame(1, face(20, .99f, "old")), 2_000_001));
        assertFalse(state.hasVisibleFaces());
    }
    @Test public void faceDisappearingClearsExactlyOnceAndNextFaceRedraws() {
        FaceSearchOverlayState state = newState();
        assertTrue(state.accept(frame(1, face(20, .99f, "A")), 0));
        assertTrue(state.accept(frame(2), 200));
        assertFalse(state.accept(frame(3), 400));
        assertTrue(state.accept(frame(4, face(30, null, null)), 600));
        assertTrue(state.hasVisibleFaces());
        assertNull(state.score(0));
    }
    @Test public void emptyFrameStillClearsVisibleContentWhenExpiryHasNotRun() {
        FaceSearchOverlayState state = newState();
        assertTrue(state.accept(frame(1, face(20, .99f, "A")), 0));
        assertTrue(state.accept(frame(2), 2000));
        assertFalse(state.hasVisibleFaces());
        assertEquals(0, state.expiresAt());
    }
    @Test public void lateResultCannotResurrectFaceAfterEmptyFrame() {
        FaceSearchOverlayState state = newState();
        FaceSearchFrame matched = frame(1, face(20, .99f, "A"));
        state.accept(matched, 0);
        state.accept(frame(2), 200);
        state.complete(new FaceSearchResult(FaceSearchResult.EndReason.MATCH_FOUND, .99f, matched), 250);
        assertFalse(state.hasVisibleFaces());
        assertNull(state.score(0));
        assertEquals(0, state.expiresAt());
    }
    @Test public void unchangedAssociationPolicyDoesNotRequestAnotherRefresh() {
        FaceSearchOverlayState state = newState();
        assertFalse(state.setAssociationEnabled(true));
        assertTrue(state.setAssociationEnabled(false));
        assertFalse(state.setAssociationEnabled(false));
        assertTrue(state.setAssociationEnabled(true));
    }
    @Test public void matchWaitsForFinalVerdictThenShowsIdSimilarityAndLiveness() {
        FaceSearchOverlayState state = newState();
        FaceSearchFrame frame = frame(1, face(20, .91234f, "A"));
        state.accept(frame, 0);
        assertEquals(FaceSearchOverlayState.COLOR_PENDING, state.color(0));
        state.complete(new FaceSearchResult(FaceSearchResult.EndReason.MATCH_FOUND, .97012f, frame), 50);
        assertEquals(FaceSearchOverlayState.COLOR_MATCHED, state.color(0));
        assertEquals("A", state.label(0));
        assertEquals("相似度 0.912 · 活体 0.970", state.details(0));
        assertEquals(1000, state.expiresAt());
    }
    @Test public void strongIdentityMatchWithLowLivenessNeverFlashesGreen() {
        FaceSearchOverlayState state = newState();
        FaceSearchFrame frame = frame(1, face(20, .99f, "A"));
        state.accept(frame, 0);
        assertEquals(FaceSearchOverlayState.COLOR_PENDING, state.color(0));
        FaceSearchResult rejected = new FaceSearchResult(FaceSearchResult.EndReason.SPOOF_DETECTED, .42f, frame);
        state.complete(rejected, 50);
        assertEquals(FaceSearchOverlayState.COLOR_UNMATCHED, state.color(0));
        assertEquals("A · 活体未通过", state.label(0));
        assertEquals("相似度 0.990 · 活体 0.420", state.details(0));
        state.accept(frame, 60);
        state.complete(rejected, 900);
        assertEquals(FaceSearchOverlayState.COLOR_UNMATCHED, state.color(0));
        assertEquals(1000, state.expiresAt());
        state.expire(1000);
        state.complete(rejected, 1001);
        assertEquals(FaceSearchOverlayState.COLOR_PENDING, state.color(0));
    }
    @Test public void livenessRejectionWithoutLookupStillShowsRealZeroOrMissingScore() {
        for (Float live : new Float[]{0f, null}) {
            FaceSearchOverlayState state = newState();
            FaceSearchFrame frame = frame(1, face(20, null, null));
            state.accept(frame, 0);
            state.complete(new FaceSearchResult(FaceSearchResult.EndReason.SPOOF_DETECTED, live, frame), 30);
            assertEquals(FaceSearchOverlayState.COLOR_UNMATCHED, state.color(0));
            assertEquals("活体未通过", state.label(0));
            assertEquals(live == null ? "" : "活体 0.000", state.details(0));
            state.accept(frame(2, face(22, null, null)), 60);
            assertEquals(FaceSearchOverlayState.COLOR_UNMATCHED, state.color(0));
            assertEquals(1030, state.expiresAt());
            state.expire(1030);
            assertEquals("", state.details(0));
        }
    }
    @Test public void lowSimilarityAndLowLivenessShowBothFailuresButKeepScoreInSdkFace() {
        FaceSearchOverlayState state = newState();
        FaceSearchFrame frame = frame(1, face(20, .7f, null));
        state.accept(frame, 0);
        state.complete(new FaceSearchResult(FaceSearchResult.EndReason.SPOOF_DETECTED, .42f, frame), 30);
        assertEquals(FaceSearchOverlayState.COLOR_UNMATCHED, state.color(0));
        assertEquals("未匹配 · 活体未通过", state.label(0));
        assertEquals("活体 0.420", state.details(0));
        assertEquals(.7f, state.score(0).getSimilarity(), 0f);
        assertNull(state.score(0).getFaceId());
    }
    @Test public void unmatchedResultHidesSimilarityAndKeepsPassedLiveness() {
        FaceSearchOverlayState state = newState();
        FaceSearchFrame frame = frame(1, face(20, .7f, null));
        state.accept(frame, 0);
        assertEquals("", state.details(0));
        state.complete(new FaceSearchResult(FaceSearchResult.EndReason.TIMEOUT, .95f, frame), 30);
        assertEquals("未匹配", state.label(0));
        assertEquals("活体 0.950", state.details(0));
        assertEquals(FaceSearchOverlayState.COLOR_UNMATCHED, state.color(0));
    }
    @Test public void finalLivenessFollowsOnlyItsOwnInheritedScoreWithoutExtendingExpiry() {
        for (boolean rejected : new boolean[]{true, false}) {
            FaceSearchOverlayState state = newState();
            FaceSearchFrame scored = frame(1, face(20, .95f, "A"));
            state.accept(scored, 0);
            FaceSearchFrame current = frame(2, face(22, null, null));
            state.accept(current, 33);
            state.complete(new FaceSearchResult(rejected ? FaceSearchResult.EndReason.SPOOF_DETECTED
                    : FaceSearchResult.EndReason.MATCH_FOUND, rejected ? .5f : .95f, scored), 50);
            assertSame(current, state.frame());
            assertEquals(rejected ? FaceSearchOverlayState.COLOR_UNMATCHED
                    : FaceSearchOverlayState.COLOR_MATCHED, state.color(0));
            assertEquals(rejected ? "相似度 0.950 · 活体 0.500"
                    : "相似度 0.950 · 活体 0.950", state.details(0));
            assertEquals(1000, state.expiresAt());
        }
    }
    @Test public void newIdentityAndDisabledLivenessDropThePreviousLivenessLabel() {
        FaceSearchOverlayState state = newState();
        FaceSearchFrame first = frame(1, face(20, .95f, "A"));
        state.accept(first, 0);
        state.complete(new FaceSearchResult(FaceSearchResult.EndReason.MATCH_FOUND, .97f, first), 10);
        FaceSearchFrame next = frame(2, face(22, .96f, "B"));
        state.accept(next, 33);
        assertEquals(FaceSearchOverlayState.COLOR_PENDING, state.color(0));
        assertEquals("相似度 0.960", state.details(0));
        state.complete(new FaceSearchResult(FaceSearchResult.EndReason.MATCH_FOUND, null, next), 40);
        assertEquals(FaceSearchOverlayState.COLOR_MATCHED, state.color(0));
        assertEquals("B", state.label(0));
        assertEquals("相似度 0.960", state.details(0));
        state.complete(new FaceSearchResult(FaceSearchResult.EndReason.SPOOF_DETECTED, .1f, first), 50);
        assertEquals(FaceSearchOverlayState.COLOR_MATCHED, state.color(0));
        assertEquals("B", state.label(0));
    }
    @Test public void lateRejectionCannotPaintAnUnrelatedNewDetection() {
        FaceSearchOverlayState state = newState();
        FaceSearchFrame old = frame(1, face(20, null, null));
        state.accept(old, 0);
        state.accept(frame(2, face(250, null, null)), 33);
        state.complete(new FaceSearchResult(FaceSearchResult.EndReason.SPOOF_DETECTED, .1f, old), 50);
        assertEquals(FaceSearchOverlayState.COLOR_PENDING, state.color(0));
        assertEquals("", state.label(0));
        assertEquals("", state.details(0));
    }
    @Test public void partialMultiTimeoutConfirmsOnlyTheScoredFacesWithoutInventingLiveness() {
        FaceSearchOverlayState state = newState();
        FaceSearchFrame frame = frame(1, face(20, .9f, "A"), face(240, .7f, null), face(430, null, null));
        state.accept(frame, 0);
        state.complete(new FaceSearchResult(FaceSearchResult.EndReason.TIMEOUT, null, frame), 50);
        assertEquals(FaceSearchOverlayState.COLOR_MATCHED, state.color(0));
        assertEquals(FaceSearchOverlayState.COLOR_UNMATCHED, state.color(1));
        assertEquals(FaceSearchOverlayState.COLOR_PENDING, state.color(2));
        assertEquals("相似度 0.900", state.details(0));
        assertEquals("未匹配", state.label(1));
        assertEquals("", state.details(1));
    }
    @Test public void aFinalResultCannotReviveAStalledFrameOrBlockANewSession() {
        FaceSearchOverlayState state = newState();
        FaceSearchFrame frame = frame(20, face(20, .9f, "A"));
        state.accept(frame, 0);
        state.complete(new FaceSearchResult(FaceSearchResult.EndReason.SPOOF_DETECTED, .1f, frame), 1200);
        assertNull(state.frame());
        state.clear();
        state.complete(new FaceSearchResult(FaceSearchResult.EndReason.MATCH_FOUND, .99f, frame), 1300);
        show(state, frame(1, face(22, .92f, "B")), 1301);
        assertEquals(FaceSearchOverlayState.COLOR_MATCHED, state.color(0));
        assertEquals("B", state.label(0));
    }
    @Test public void fiveFaceBuffersDoNotLeakRemovedScoresBackIntoNewSlots() {
        FaceSearchOverlayState state=newState();
        show(state, frame(1,face(0,.9f,"A"),face(120,.9f,"B"),face(240,.9f,"C"),
                face(360,.9f,"D"),face(480,.9f,"E")),0);
        show(state, frame(2,face(242,null,null)),33);
        assertEquals("C",state.score(0).getFaceId());
        show(state, frame(3,face(0,null,null),face(120,null,null),face(244,null,null),
                face(360,null,null),face(480,null,null)),66);
        for(int i=0;i<5;i++) {
            if(i==2)assertEquals("C",state.score(i).getFaceId());
            else {assertNull(state.score(i));assertEquals("",state.label(i));}
        }
        state.clear();show(state, frame(1,face(0,null,null)),99);assertNull(state.score(0));
    }
    @Test public void labelIsFormattedOncePerScoreAndClearedOnRejectionOrExpiry() {
        FaceSearchOverlayState state=newState();show(state, frame(1,face(20,.91234f,"A")),0);
        String label=state.label(0);assertEquals("A",label);
        String details=state.details(0);assertEquals("相似度 0.912",details);
        show(state, frame(2,face(25,null,null)),33);assertSame(label,state.label(0));assertSame(details,state.details(0));
        state.expire(1000);assertEquals("",state.label(0));
        show(state, frame(3,face(25,.8f,"B")),1001);assertEquals("B",state.label(0));assertEquals("相似度 0.800",state.details(0));
        state.rejectScoresThrough(3);assertEquals("",state.label(0));
    }
    private static FaceSearchOverlayState newState() {
        return new FaceSearchOverlayState("相似度", "活体", "活体未通过", "未匹配");
    }
    // Existing geometric-association regressions use confirmed matches; new tests cover onFrame/onResult
    // interleaving separately.
    // 原有几何关联回归使用已确认的匹配；新测试单独覆盖 onFrame/onResult 的交错。
    private static void show(FaceSearchOverlayState state, FaceSearchFrame frame, long now) {
        state.accept(frame, now);
        for (FaceSearchFace face : frame.getFaces()) {
            if (face.isThresholdPassed()) {
                state.complete(new FaceSearchResult(FaceSearchResult.EndReason.MATCH_FOUND, null, frame), now);
                break;
            }
        }
    }
    private static FaceSearchFace face(float x, Float score, String id) {
        boolean passed=score!=null && score>=.8f;
        return new FaceSearchFace(new FaceSearchFace.Bounds(x,30,150,150),passed,score,passed?id:null,"","");
    }
    private static FaceSearchFrame frame(long id,FaceSearchFace... faces) {
        return new FaceSearchFrame(id,640,480,Arrays.asList(faces));
    }
    private static int color(FaceSearchOverlayState state,int index) {
        return state.color(index);
    }
    @Test public void stationaryPendingFrameRetainsOnlyDisplayScoreAndNeverMutatesSdkFrame() {
        FaceSearchOverlayState state=newState();show(state, frame(1,face(20,.9f,"A")),0);
        FaceSearchFrame pending=frame(2,face(20,null,null));show(state, pending,33);
        assertSame(pending,state.frame());assertNull(pending.getFaces().get(0).getFaceId());
        assertNull(pending.getFaces().get(0).getSimilarity());
        assertEquals(FaceSearchOverlayState.COLOR_MATCHED,color(state,0));assertEquals("A",state.score(0).getFaceId());
    }
    @Test public void evenSubpixelMovementUsesLatestBoundsAndDoesNotRenewTheScore() {
        FaceSearchOverlayState state=newState();show(state, frame(1,face(20,.9f,"A")),0);
        for(int i=1;i<=30;i++) {
            FaceSearchFrame moving=frame(1+i,face(20+i*.1f,null,null));show(state, moving,i*33);
            assertSame(moving,state.frame());assertEquals("A",state.score(0).getFaceId());
            assertEquals(1000,state.expiresAt());
        }
        state.expire(1000);assertNull(state.score(0));assertNotNull(state.frame());
    }
    @Test public void sameFrameScoringFollowsGeometryIntoTheNextFrame() {
        FaceSearchOverlayState state=newState();show(state, frame(1,face(20,null,null)),0);
        show(state, frame(1,face(20,.9f,"A")),50);
        FaceSearchFrame next=frame(2,face(30,null,null));show(state, next,66);
        assertSame(next,state.frame());assertEquals("A",state.score(0).getFaceId());assertEquals(1050,state.expiresAt());
    }
    @Test public void newLowScoreImmediatelyOverridesGreenAndPendingCanRetainRed() {
        for(float score:new float[]{0f,.79f}) {
            FaceSearchOverlayState state=newState();show(state, frame(1,face(20,.9f,"A")),0);
            show(state, frame(2,face(20,score,null)),33);
            assertEquals(FaceSearchOverlayState.COLOR_UNMATCHED,color(state,0));assertNull(state.score(0).getFaceId());
            assertEquals("未匹配",state.label(0));
            show(state, frame(3,face(20,null,null)),66);
            assertEquals(FaceSearchOverlayState.COLOR_UNMATCHED,color(state,0));assertEquals(1033,state.expiresAt());
        }
    }
    @Test public void freshMatchUpdatesIdentityAndScoreImmediately() {
        FaceSearchOverlayState state=newState();show(state, frame(1,face(20,.9f,"A")),0);
        show(state, frame(2,face(25,.95f,"B")),100);
        assertEquals("B",state.score(0).getFaceId());assertEquals(.95f,state.score(0).getSimilarity(),0f);
    }
    @Test public void colorUsesNullableScoreAndSdkThresholdDecision() {
        FaceSearchFace.Bounds bounds=new FaceSearchFace.Bounds(20,30,150,150);
        assertEquals(FaceSearchOverlayState.COLOR_MATCHED,FaceSearchOverlayState.colorOf(new FaceSearchFace(bounds,true,.78f,"A","","")));
        assertEquals(FaceSearchOverlayState.COLOR_UNMATCHED,FaceSearchOverlayState.colorOf(new FaceSearchFace(bounds,false,.82f,null,"","")));
        assertEquals(FaceSearchOverlayState.COLOR_PENDING,FaceSearchOverlayState.colorOf(new FaceSearchFace(bounds,true,null,"A","","")));
        assertEquals(FaceSearchOverlayState.COLOR_PENDING,FaceSearchOverlayState.colorOf(null));
    }
    @Test public void disappearanceClearsHistoryAndReappearingFaceStartsPending() {
        FaceSearchOverlayState state=newState();show(state, frame(1,face(20,.9f,"A")),0);
        FaceSearchFrame empty=frame(2);show(state, empty,33);
        assertSame(empty,state.frame());assertEquals(0,state.expiresAt());
        show(state, frame(1,face(20,.99f,"B")),66);assertSame(empty,state.frame());
        show(state, frame(3,face(20,null,null)),99);assertNull(state.score(0));
    }
    @Test public void lateOlderScoreCannotReplaceNewGeometryOrExtendDeadline() {
        FaceSearchOverlayState state=newState();FaceSearchFrame latest=frame(3,face(200,null,null));show(state, latest,100);
        long deadline=state.expiresAt();show(state, frame(2,face(20,.9f,"A")),200);
        assertSame(latest,state.frame());assertNull(state.score(0));assertEquals(deadline,state.expiresAt());
    }
    @Test public void repeatedSameFrameScoreCannotRenewOrReviveExpiredScore() {
        FaceSearchOverlayState state=newState();FaceSearchFrame scored=frame(1,face(20,.9f,"A"));show(state, scored,0);
        show(state, scored,900);assertEquals(1000,state.expiresAt());
        state.expire(1000);show(state, scored,1001);assertNull(state.score(0));assertEquals(1200,state.expiresAt());
    }
    @Test public void sameFrameUpdatesCannotExtendOrReviveAStalledImage() {
        FaceSearchOverlayState state=newState();show(state, frame(1,face(20,null,null)),0);
        show(state, frame(1,face(20,.9f,"A")),1199);assertEquals(1200,state.expiresAt());
        state.expire(1200);assertNull(state.frame());assertEquals(0,state.expiresAt());
        show(state, frame(1,face(20,.99f,"A")),1201);assertNull(state.frame());
        show(state, frame(2,face(22,null,null)),1202);assertNull(state.score(0));assertEquals(2402,state.expiresAt());
    }
    @Test public void lateSameFrameScoreExpiresWithoutWaitingForViewTimer() {
        FaceSearchOverlayState state=newState();show(state, frame(1,face(20,null,null)),0);
        show(state, frame(1,face(20,.9f,"A")),1200);assertNull(state.frame());
    }
    @Test public void reorderedAndRemovedFacesKeepOnlyTheirOwnGeometricHistory() {
        FaceSearchOverlayState state=newState();
        show(state, frame(1,face(20,.9f,"A"),face(240,.6f,null),face(430,null,null)),0);
        FaceSearchFrame reordered=frame(2,face(430,null,null),face(25,null,null),face(245,null,null));show(state, reordered,33);
        assertSame(reordered,state.frame());assertNull(state.score(0));
        assertEquals("A",state.score(1).getFaceId());assertEquals(FaceSearchOverlayState.COLOR_UNMATCHED,color(state,2));
        show(state, frame(3,face(250,null,null)),66);assertEquals(FaceSearchOverlayState.COLOR_UNMATCHED,color(state,0));
        show(state, frame(4,face(20,null,null),face(255,null,null)),99);assertNull(state.score(0));
    }
    @Test public void overlappingAndCrossingFacesDropInheritedIdentities() {
        FaceSearchOverlayState state=newState();show(state, frame(1,face(20,.9f,"A"),face(185,.95f,"B")),0);
        show(state, frame(2,face(50,null,null),face(155,null,null)),33);
        show(state, frame(3,face(80,null,null),face(125,null,null)),66);
        assertNull(state.score(0));assertNull(state.score(1));
        show(state, frame(4,face(155,null,null),face(50,null,null)),99);
        assertNull(state.score(0));assertNull(state.score(1));
    }
    @Test public void oneOldFaceCannotSupplyTwoNewFaces() {
        FaceSearchOverlayState state=newState();show(state, frame(1,face(100,.9f,"A")),0);
        show(state, frame(2,face(80,null,null),face(120,null,null)),33);
        assertNull(state.score(0));assertNull(state.score(1));
    }
    @Test public void longDetectionGapOrLargeMovementDropsHistory() {
        for(boolean gap:new boolean[]{true,false}) {
            FaceSearchOverlayState state=newState();show(state, frame(1,face(20,.9f,"A")),0);
            show(state, frame(2,face(gap?20:80,null,null)),gap?301:33);assertNull(state.score(0));
        }
    }
    @Test public void largeSizeChangeDropsHistory() {
        FaceSearchOverlayState state=newState();show(state, frame(1,face(20,.9f,"A")),0);
        FaceSearchFace smaller=new FaceSearchFace(new FaceSearchFace.Bounds(60,70,70,70),false,null,null,"","");
        show(state, frame(2,smaller),33);assertNull(state.score(0));
    }
    @Test public void singleModeWithMultipleDetectionsDisablesInheritanceUntilAnUnambiguousNewFrame() {
        FaceSearchOverlayState state=newState();show(state, frame(1,face(20,.9f,"A")),0);
        show(state, frame(2,face(22,null,null)),33);state.setAssociationEnabled(false);assertNull(state.score(0));
        show(state, frame(2,face(22,.9f,"A")),40);assertEquals("A",state.score(0).getFaceId());
        show(state, frame(3,face(23,null,null)),66);assertNull(state.score(0));
        state.setAssociationEnabled(true);show(state, frame(4,face(24,null,null)),99);assertNull(state.score(0));
        show(state, frame(4,face(24,.9f,"A")),105);show(state, frame(5,face(25,null,null)),132);
        assertEquals("A",state.score(0).getFaceId());
    }
    @Test public void failedRoundClearsOlderScoresWithoutErasingANewerScore() {
        FaceSearchOverlayState state=newState();show(state, frame(1,face(20,.9f,"A")),0);
        show(state, frame(2,face(22,null,null)),33);state.rejectScoresThrough(2);assertNull(state.score(0));
        show(state, frame(2,face(22,.9f,"A")),40);assertNull(state.score(0));
        show(state, frame(3,face(24,.95f,"B")),66);state.rejectScoresThrough(2);assertEquals("B",state.score(0).getFaceId());
    }
    @Test public void imageSizeChangeDoesNotTransferIdentity() {
        FaceSearchOverlayState state=newState();show(state, frame(1,face(20,.9f,"A")),0);
        show(state, new FaceSearchFrame(2,480,640,Arrays.asList(face(20,null,null))),33);assertNull(state.score(0));
    }
    @Test public void timeoutInvalidatesInheritedGreenButKeepsTheActualLowScoreFromItsFrame() {
        FaceSearchOverlayState state=newState();
        show(state, frame(1,face(20,.9f,"A"),face(240,.95f,"B")),0);
        show(state, frame(2,face(22,null,null),face(242,.7f,null)),33);
        state.rejectScoresThrough(1);
        assertNull(state.score(0));assertEquals(FaceSearchOverlayState.COLOR_UNMATCHED,color(state,1));
    }
    @Test public void clearAllowsNewSessionNumbersAndDiscardsExpiredAndRejectedState() {
        FaceSearchOverlayState state=newState();show(state, frame(20,face(20,.9f,"A")),0);state.rejectScoresThrough(20);
        state.clear();assertNull(state.frame());assertEquals(-1,state.newestFrameId());assertEquals(0,state.expiresAt());
        show(state, frame(1,face(20,.9f,"B")),100);assertEquals("B",state.score(0).getFaceId());
    }
}
