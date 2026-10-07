#include "application/recognition/face_recognition.hpp"

#include <limits>

#include <gtest/gtest.h>

#include "domain/vision/face_association.hpp"
#include "support/builders.hpp"
namespace vqec::test {
namespace {
domain::Embedding vector(float x, float y) {
    std::vector<float> v(512, 0);
    v[0] = x;
    v[1] = y;
    return {"edgeface_s_gamma_05_v1", v};
}
domain::Track person() {
    auto t = make_track(7, 0.5F, 0.5F);
    t.box = {0, 0, 200, 400};
    return t;
}
domain::Detection face() {
    return {{50, 50, 50, 60}, 0, "face", 0.9F, std::array<domain::Point2f, 5>{}};
}
}  // namespace
TEST(FaceRecognition, RanksDistinctEmployeesNotPhotos) {
    std::vector<domain::FaceGallerySample> gallery{
        {"TECH1", vector(1, 0)}, {"TECH1", vector(1, 0)}, {"TECH2", vector(0, 1)}};
    auto match = domain::match_face(gallery, vector(1, 0));
    ASSERT_TRUE(match.ok());
    EXPECT_EQ(match.value().employee_id, "TECH1");
    EXPECT_FLOAT_EQ(match.value().second_score.value(), 0);
}
TEST(FaceRecognition, ConfirmsClearMatchOnceAndRequiresTwoWeakMatches) {
    auto frame = make_frame(1);
    app::FaceRecognition recognition({}, {{"TECH1", vector(1, 0)}, {"TECH2", vector(0, 1)}});
    auto jobs = recognition.select(frame, {person()}, {face()});
    ASSERT_EQ(jobs.size(), 1U);
    auto events = recognition.apply(frame, jobs, {vector(1, 0)});
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].attributes.at("employee_id"), "TECH1");
    EXPECT_TRUE(recognition.select(frame, {person()}, {face()}).empty());
    recognition.reset();
    jobs = recognition.select(frame, {person()}, {face()});
    EXPECT_TRUE(recognition.apply(frame, jobs, {vector(0.72F, 0.693974F)}).empty());
    frame.sequence = 2;
    jobs = recognition.select(frame, {person()}, {face()});
    EXPECT_EQ(recognition.apply(frame, jobs, {vector(0.72F, 0.693974F)}).size(), 1U);
}
TEST(FaceRecognition, MissingFaceInterruptsWeakConfirmationAndAmbiguousOwnershipIsRejected) {
    auto frame = make_frame(1);
    app::FaceRecognition recognition({}, {{"TECH1", vector(1, 0)}, {"TECH2", vector(0, 1)}});
    auto jobs = recognition.select(frame, {person()}, {face()});
    recognition.apply(frame, jobs, {vector(0.72F, 0.693974F)});
    frame.sequence = 2;
    recognition.select(frame, {person()}, {});
    frame.sequence = 3;
    jobs = recognition.select(frame, {person()}, {face()});
    EXPECT_TRUE(recognition.apply(frame, jobs, {vector(0.72F, 0.693974F)}).empty());
    auto other = person();
    other.id = 8;
    EXPECT_TRUE(recognition.select(frame, {person(), other}, {face()}).empty());
}
}  // namespace vqec::test
namespace vqec::test {
TEST(FaceRecognition, BudgetDefersFairlyAndExpiredTracksDoNotInheritIdentity) {
    auto frame = make_frame(1);
    app::FaceRecognition recognition({}, {{"TECH1", vector(1, 0)}});
    std::vector<domain::Track> tracks;
    std::vector<domain::Detection> faces;
    for (std::uint64_t i = 1; i <= 5; ++i) {
        auto t = person();
        t.id = i;
        t.box.x = static_cast<float>(i * 100);
        t.box.width = 80;
        tracks.push_back(t);
        auto f = face();
        f.box.x = t.box.x + 10;
        f.box.width = 25;
        f.box.height = 25;
        faces.push_back(f);
    }
    auto jobs = recognition.select(frame, tracks, faces);
    ASSERT_EQ(jobs.size(), 4U);
    ports::EmbeddingBatch matches;
    for (std::size_t i = 0; i < 4; ++i)
        matches.emplace_back(vector(1, 0));
    EXPECT_EQ(recognition.apply(frame, jobs, matches).size(), 4U);
    frame.sequence = 2;
    jobs = recognition.select(frame, tracks, faces);
    ASSERT_EQ(jobs.size(), 1U);
    EXPECT_EQ(jobs[0].track_id, 5U);
    recognition.select(frame, {}, {});
    jobs = recognition.select(frame, {tracks[0]}, {faces[0]});
    ASSERT_EQ(jobs.size(), 1U);
    EXPECT_EQ(recognition.apply(frame, jobs, {vector(1, 0)}).size(), 1U);
    auto tiny = faces[0];
    tiny.box.width = 19;
    EXPECT_TRUE(recognition.select(frame, {tracks[0]}, {tiny}).empty());
}
TEST(FaceRecognition, IdentityChangeRequiresTwoResultsAndRefreshDoesNotRepeatLog) {
    auto frame = make_frame(1);
    app::FaceRecognition recognition({}, {{"TECH1", vector(1, 0)}, {"TECH2", vector(0, 1)}});
    auto jobs = recognition.select(frame, {person()}, {face()});
    EXPECT_EQ(recognition.apply(frame, jobs, {vector(1, 0)}).size(), 1U);
    frame.pts = std::chrono::seconds(2);
    jobs = recognition.select(frame, {person()}, {face()});
    ASSERT_EQ(jobs.size(), 1U);
    EXPECT_TRUE(recognition.apply(frame, jobs, {vector(0, 1)}).empty());
    frame.pts = std::chrono::milliseconds(2100);
    jobs = recognition.select(frame, {person()}, {face()});
    ASSERT_EQ(jobs.size(), 1U);
    EXPECT_EQ(recognition.apply(frame, jobs, {vector(0, 1)}).size(), 1U);
    frame.pts = std::chrono::seconds(5);
    jobs = recognition.select(frame, {person()}, {face()});
    EXPECT_TRUE(recognition.apply(frame, jobs, {vector(0, 1)}).empty());
}
}  // namespace vqec::test

namespace vqec::test {
TEST(FaceRecognition, RejectsIncompatibleNonfiniteAndUnnormalizedGalleryEvidence) {
    auto bad = vector(1, 0);
    bad.model_id = "arcface";
    EXPECT_FALSE(domain::match_face({{"TECH1", vector(1, 0)}}, bad).ok());
    bad = vector(1, 0);
    bad.values.resize(511);
    EXPECT_FALSE(domain::validate_face_embedding(bad).ok());
    bad = vector(2, 0);
    EXPECT_FALSE(domain::validate_face_embedding(bad).ok());
    bad.values[0] = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(domain::validate_face_embedding(bad).ok());
}
TEST(FaceRecognition, RejectsTwoFacesForOnePersonAndTentativeOrLostOwners) {
    auto f = face();
    auto second = face();
    second.box.x += 60;
    const auto owners = domain::associate_faces({person()}, {f, second});
    ASSERT_EQ(owners.size(), 2U);
    EXPECT_FALSE(owners[0]);
    EXPECT_FALSE(owners[1]);
    for (const auto state : {domain::TrackState::Tentative, domain::TrackState::Lost}) {
        auto t = person();
        t.state = state;
        EXPECT_FALSE(domain::associate_faces({t}, {f})[0]);
    }
}
TEST(FaceRecognition, EnforcesExactSourceBoundariesAndBoundedStateWithLostRetention) {
    auto frame = make_frame(1);
    app::FaceRecognitionOptions options;
    options.max_tracks = 1;
    app::FaceRecognition recognition(options, {{"TECH1", vector(1, 0)}});
    auto f = face();
    f.box.width = 20;
    f.box.height = 25;
    auto jobs = recognition.select(frame, {person()}, {f});
    ASSERT_EQ(jobs.size(), 1U);
    EXPECT_EQ(recognition.apply(frame, jobs, {vector(1, 0)}).size(), 1U);
    auto lost = person();
    lost.state = domain::TrackState::Lost;
    EXPECT_TRUE(recognition.select(frame, {lost}, {f}).empty());
    EXPECT_TRUE(recognition.select(frame, {person()}, {f}).empty());
    auto other = person();
    other.id = 8;
    other.box.x = 300;
    auto other_face = f;
    other_face.box.x += 300;
    EXPECT_TRUE(recognition.select(frame, {person(), other}, {f, other_face}).empty());
    recognition.reset();
    f.box.height = 24;
    EXPECT_TRUE(recognition.select(frame, {person()}, {f}).empty());
    f.box.width = 19;
    f.box.height = 30;
    EXPECT_TRUE(recognition.select(frame, {person()}, {f}).empty());
    f.box.width = 20;
    f.box.height = 25;
    jobs = recognition.select(frame, {person()}, {f});
    EXPECT_EQ(jobs.size(), 1U);
}
TEST(FaceRecognition, UnknownAndFailedEvidenceInterruptWeakVotes) {
    auto frame = make_frame(1);
    app::FaceRecognition recognition({}, {{"TECH1", vector(1, 0)}, {"TECH2", vector(0, 1)}});
    for (const auto failure : {false, true}) {
        recognition.reset();
        auto jobs = recognition.select(frame, {person()}, {face()});
        EXPECT_TRUE(recognition.apply(frame, jobs, {vector(.72F, .693974F)}).empty());
        jobs = recognition.select(frame, {person()}, {face()});
        ports::EmbeddingBatch rejected;
        if (failure)
            rejected.emplace_back(make_error(ErrorCode::Internal, "failed"));
        else
            rejected.emplace_back(vector(-1, 0));
        EXPECT_TRUE(recognition.apply(frame, jobs, rejected).empty());
        jobs = recognition.select(frame, {person()}, {face()});
        EXPECT_TRUE(recognition.apply(frame, jobs, {vector(.72F, .693974F)}).empty());
    }
}
}  // namespace vqec::test
namespace vqec::test {
TEST(FaceRecognition, KeepsEmployeeForVisibleTrackWithoutFaceAndClearsOnExpiryOrReset) {
    auto frame = make_frame(1);
    app::FaceRecognition recognition({}, {{"TECH1", vector(1, 0)}});
    auto jobs = recognition.select(frame, {person()}, {face()});
    recognition.apply(frame, jobs, {vector(1, 0)});
    recognition.select(frame, {person()}, {});
    ports::FrameAnnotations annotations;
    annotations.tracks = {person()};
    recognition.annotate(frame, annotations);
    ASSERT_EQ(annotations.employee_ids.at(7), "TECH1");
    recognition.select(frame, {}, {});
    annotations.employee_ids.clear();
    recognition.annotate(frame, annotations);
    EXPECT_TRUE(annotations.employee_ids.empty());
    jobs = recognition.select(frame, {person()}, {face()});
    recognition.apply(frame, jobs, {vector(1, 0)});
    recognition.reset();
    recognition.annotate(frame, annotations);
    EXPECT_TRUE(annotations.employee_ids.empty());
}
TEST(FaceRecognition, UnknownAndBackwardCrossingsDoNotEmitAttendanceOrQueueLaterEvents) {
    auto frame = make_frame(1);
    app::FaceRecognition recognition({}, {{"TECH1", vector(1, 0)}});
    recognition.select(frame, {person()}, {face()});
    ports::FrameAnnotations annotations;
    annotations.tracks = {person()};
    domain::Event crossing;
    crossing.type = "line_crossing.cross";
    crossing.track_id = 7;
    crossing.attributes["direction"] = "forward";
    annotations.events = {crossing};
    recognition.annotate(frame, annotations);
    ASSERT_EQ(annotations.events.size(), 1U);
    auto jobs = recognition.select(frame, {person()}, {face()});
    recognition.apply(frame, jobs, {vector(1, 0)});
    annotations.events.clear();
    recognition.annotate(frame, annotations);
    EXPECT_TRUE(annotations.events.empty());
    crossing.attributes["direction"] = "backward";
    annotations.events = {crossing};
    recognition.annotate(frame, annotations);
    EXPECT_EQ(annotations.events.size(), 1U);
}
}  // namespace vqec::test
