#include "Cube5x5/Solver5x5_Optimal.hpp"
#include "Common/TrainedSolverCore.hpp"

void Solver5x5_Optimal::initTables() {
    Solver3x3_Min2Phase::initTables();
    TrainedSolver::engineCtx().ensureInitialized();
}

SolveResult Solver5x5_Optimal::solve(const uint8_t colors[150]) {
    if (!State5x5::validateFacelets(colors)) {
        SolveResult err;
        err.isValid = false;
        err.errorMessage = "Invalid 5x5 cube colors.";
        err.totalMoves = 0;
        err.solveTimeMs = 0;
        return err;
    }
    State5x5 state = State5x5::fromFacelets(colors);
    return solveState(state);
}

SolveResult Solver5x5_Optimal::solveState(const State5x5& state) {
    return TrainedSolver::solve5x5WithWeights(state, TrainedSolver::engineCtx().bank.w5x5, true);
}
