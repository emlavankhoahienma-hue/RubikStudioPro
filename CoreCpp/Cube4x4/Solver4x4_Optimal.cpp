#include "Cube4x4/Solver4x4_Optimal.hpp"
#include "Common/TrainedSolverCore.hpp"

void Solver4x4_Optimal::initTables() {
    Solver3x3_Min2Phase::initTables();
    TrainedSolver::engineCtx().ensureInitialized();
}

SolveResult Solver4x4_Optimal::solve(const uint8_t colors[96]) {
    if (!State4x4::validateFacelets(colors)) {
        SolveResult err;
        err.isValid = false;
        err.errorMessage = "Invalid 4x4 cube colors.";
        err.totalMoves = 0;
        err.solveTimeMs = 0;
        return err;
    }
    State4x4 state = State4x4::fromFacelets(colors);
    return solveState(state);
}

SolveResult Solver4x4_Optimal::solveState(const State4x4& state) {
    return TrainedSolver::solve4x4WithWeights(state, TrainedSolver::engineCtx().bank.w4x4, true);
}
