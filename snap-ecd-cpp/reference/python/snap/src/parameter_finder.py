# Author: Nicholas Bornman & Tanay Roy

import numpy as np
from scipy import sparse
import scipy
from scipy.optimize import minimize


__all__ = ['displacement', 'snap', 'add_buffer_levels', 'ansatzU', 'construct_U_realized', 
           'cost_unitary', 'cost_state', 'gradient_cost_unitary', 'gradient_cost_state',
           'gradient_cost_unitary_flat', 'gradient_cost_state_flat',
           'params_to_alphas', 'params_to_thetas',
           'alphas_and_thetas_to_params', 'cost_unitaries', 'cost_states',
           'pulse_parameter_finder']


def displacement(alpha, n_dim):
    """
    Displacement gate. A n_dim-dimensional matrix representation of a
    displacement gate.

    Note: we probably want to set n_dim to be equal to the cavity dimension
    as a displacement will act on all levels of the cavity in practice.

    Parameters
    ----------
    alpha : float
        the displacement parameter
    n_dim : int
        size of matrix representation of displacement gate

    Returns
    -------
    2D numpy array
        (n_dim) by (n_dim) matrix representation of gate.
    """

    # list of square root of positive integers from 1 to n_dim-1
    upper_diag = list(map(lambda x: np.sqrt(x), range(1, n_dim)))

    # matrix representation of a and a^{dagger}
    a = sparse.diags(upper_diag, offsets=1).toarray()
    adag = a.conj().T

    # eigensystem of the matrix "adag - a"
    # L -> eigenvalues; P -> array of corresponding eigenvectors
    # this is used to diagonalise adag - a for computational
    # efficiency (mainly important for later functions)
    L, P = scipy.linalg.eig(adag - a)

    # Pp -> adjoint of P
    Pp = P.conj().T

    # construct displacement gate using L and P
    result = P @ np.diag(np.exp(alpha * L)) @ Pp

    return result


def snap(theta_vector, n_dim):
    """
    SNAP gate. Applies non-zero SNAP phases to the first n_level Fock
    states in an n_dim matrix representation (where
    n_level = len(theta_vector)). So the phase applied to the
    i'th Fock state is (potentially) non-zero for 0 <= i < n_levels
    (we must have n_levels <= n_dim).

    Parameters
    ----------
    theta_vector : 1D numpy array of floats
        angles of phases aplied to first len(theta_vector) Fock levels
    n_dim : int
        size of matrix representation of SNAP gate

    Returns
    -------
    2D numpy array
        (n_dim) by (n_dim) matrix representation of SNAP gate.
    """

    # first n_levels states a potentially non-zero phase is added to by
    # SNAP gate
    n_levels = len(theta_vector)

    # check that theta_vector isn't longer than matrix dimension
    if n_levels > n_dim:
        raise ValueError("theta_vector too long.")

    # change angles to pure phases, and add 0 phases to the higher states
    phases = np.exp(1j*np.concatenate((theta_vector,
                                       np.zeros(n_dim - n_levels))))

    # construct the matrix with the phases on the diagonals
    result = sparse.diags(phases).toarray()

    return result

def add_buffer_levels(qudit_gate, d_cut=12):
    """
    Sets total gate dimension to be d_cut = cutoff dimension
    """
    d = qudit_gate.shape[0] # Computational space dimension
    return np.block([
                    [qudit_gate, np.zeros((d,d_cut-d))],
                    [np.zeros((d_cut-d,d)), np.eye(d_cut-d)]
                    ])

def unitary_fidelity(Uid, U):
    d = Uid.shape[0]
    # Using https://journals.aps.org/prl/supplemental/10.1103/PhysRevLett.127.130501/maple_supp.pdf
    # return (np.abs(np.trace(Uid.conj().T @ U))**2/d + 1)/(d+1)
    return np.abs(np.trace((Uid.conj().T) @ U)) / d


def ansatzU(alphas, thetas, n_dim):
    """
    ansatzU gives the unitary constructed from the
    displacement+SNAP gate ansatz

    Parameters
    ----------
    alphas : 1D numpy array of floats
        array of displacement parameters (the first element of alphas
        corresponds with the first (in time, i.e. rightmost matrix)
        displacement gate, etc.)
    thetas : 2D numpy array of floats
        array of angles for SNAP gates. The outermost index i gives an
        array corresponding to the angles applied in the i+1'th SNAP gate
        (0 <= i < thetas.shape[0], and thetas.shape[0] = k must equal
        1 less than the length of alphas). thetas[0] (with
        length <= n_dim) corresponds with the first SNAP gate above,
        thetas[1], the second SNAP gate, etc. All thetas[i] arrays must
        have the same length.
    n_dim : int
        size of matrix representation of unitary

    Returns
    -------
    2D numpy array
        (n_dim) by (n_dim) matrix representation of ansatz unitary.
    """

    # dimensions checks
    k = thetas.shape[0]

    if len(alphas) != k + 1:
        raise ValueError("Length of alpha array should equal one plus \
                         lengths of thetas array.")

    n_levels = thetas.shape[1]

    if n_levels > n_dim:
        raise ValueError("Check theta subarray lengths.")

    # same as in displacement function definition
    upper_diag = list(map(lambda x: np.sqrt(x), range(1, n_dim)))

    a = sparse.diags(upper_diag, offsets=1).toarray()
    adag = a.conj().T

    L, P = scipy.linalg.eig(adag - a)

    Pp = P.conj().T

    # starting matrix - modified step by step below
    matrix = np.eye(n_dim)

    # construct ansatz step by step, using L and P to avoid having
    # to constantly find matrix exponentials, which is computationally
    # expensive
    for i in range(0, k):

        # displacement gate
        disp = P @ np.diag(np.exp(alphas[i] * L)) @ Pp

        # SNAP gate
        phases = np.exp(1j*np.concatenate((thetas[i],
                                           np.zeros(n_dim - n_levels))))

        snp = sparse.diags(phases).toarray()

        matrix = snp @ disp @ matrix

    # add final displacement gate
    matrix = P @ np.diag(np.exp(alphas[k] * L)) @ Pp @ matrix

    return matrix

def construct_U_realized(params_1d, d_cut, n_snap):
    """
    Matrix representation of constructed unitary from the 
    optimized S+D parameters with larger cut-off dimension

    Parameters
    ----------
    params_1d : 1D numpy array of floats
        array of displacement and snap parameters 
    d_cut : int
        cut_off dimension >= qudit_dim
    n_snap : int
        number of snap gates

    Returns
    -------
    2D numpy array
        (d_cut) by (d_cut) matrix representation of the unitary.
    """
    disp_arr = params_1d[:n_snap+1]
    snap_arr = params_1d[n_snap+1:]
    d_snap = int(len(snap_arr)/n_snap)
    snap_arr = snap_arr.reshape(n_snap, d_snap)
    return ansatzU(disp_arr, snap_arr, d_cut)


def cost_unitary(U_target, alphas, thetas, n_dim, d_fid=None):
    """
    cost_unitary function used in optimiser, and as general figure of merit.
    This cost function uses a target unitary in the cost function.

    Parameters
    ----------
    U_target : 2D numpy array of complex floats
        representation of target unitary. U_target must be an (n_dim)
        by (n_dim) matrix.
    alphas : 1D numpy array of floats
        array of displacement parameters (the first element of array
        corresponds with the first (in time, i.e. rightmost matrix)
        displacement gate, etc.)
    thetas : 2D numpy array of floats
        array of angles for SNAP gates. The outermost index i gives an
        array corresponding to the angles applied in the i+1'th SNAP gate
        (0 <= i < thetas.shape[0]). thetas[i] (with length <= n_dim)
        corresponds with the first SNAP gate above, thetas[1], the second
        SNAP gate, etc. All thetas[i] arrays must have the same length.
    n_dim : int
        size of matrix representation of ansatz unitary
    d_fid : int
        Square sub_matrix dimension of the full unitary for cost calculation

    Returns
    -------
    float
        the cost function evaluated between the target unitary matrix,
        and the unitary that is reconstructed from the alphas and thetas
        input parameter arrays
    """

    # check matrix sizes are compatible
    if U_target.shape != (n_dim, n_dim):
        raise Exception("U_target size incompatible.")

    k = thetas.shape[0]

    if len(alphas) != k + 1:
        raise ValueError("Length of alpha array should equal one plus\
                         lengths of thetas array.")

    n_levels = thetas.shape[1]

    if n_levels > n_dim:
        raise ValueError("Check theta subarray lengths.")

    # same as in displacement function definition
    upper_diag = list(map(lambda x: np.sqrt(x), range(1, n_dim)))

    a = sparse.diags(upper_diag, offsets=1).toarray()
    adag = a.conj().T

    L, P = scipy.linalg.eig(adag - a)

    Pp = P.conj().T

    # two initialised matrices
    matrix = np.eye(n_dim)
    temp = np.eye(n_dim)

    # construct ansatz step by step
    for i in range(0, k):
        # displacement gate
        matrix = Pp @ temp
        temp = np.diag(np.exp(alphas[i] * L)) @ matrix
        matrix = P @ temp

        # snap gate
        temp = sparse.diags(np.exp(
            1j*np.concatenate((thetas[i], np.zeros(n_dim - n_levels))
                              ))) @ matrix

    # final displacement gate
    matrix = Pp @ temp
    temp = np.diag(np.exp(alphas[k] * L)) @ matrix
    matrix = P @ temp
    
    # Extract sub-square matrix with dimension d_fid for cost calculation
    if d_fid is not None and d_fid<n_dim:
        U_target = U_target[0:d_fid,0:d_fid]
        matrix = matrix[0:d_fid,0:d_fid]
        n_dim = d_fid

    # cost function using 'matrix' ansatz and U_target
    c = np.trace((U_target.conj().T) @ matrix) / n_dim
    cost = 1 - np.linalg.norm(c)

    # return cost_unitaries(U_target, matrix)
    return cost


def cost_unitary_old(U_target, alphas, thetas, n_dim):
    """
    cost_unitary function used in optimiser, and as general figure of merit.
    This cost function uses a target unitary in the cost function.

    Parameters
    ----------
    U_target : 2D numpy array of complex floats
        representation of target unitary. U_target must be an (n_dim)
        by (n_dim) matrix.
    alphas : 1D numpy array of floats
        array of displacement parameters (the first element of array
        corresponds with the first (in time, i.e. rightmost matrix)
        displacement gate, etc.)
    thetas : 2D numpy array of floats
        array of angles for SNAP gates. The outermost index i gives an
        array corresponding to the angles applied in the i+1'th SNAP gate
        (0 <= i < thetas.shape[0]). thetas[i] (with length <= n_dim)
        corresponds with the first SNAP gate above, thetas[1], the second
        SNAP gate, etc. All thetas[i] arrays must have the same length.
    n_dim : int
        size of matrix representation of ansatz unitary

    Returns
    -------
    float
        the cost function evaluated between the target unitary matrix,
        and the unitary that is reconstructed from the alphas and thetas
        input parameter arrays
    """

    # check matrix sizes are compatible
    if U_target.shape != (n_dim, n_dim):
        raise Exception("U_target size incompatible.")

    k = thetas.shape[0]

    if len(alphas) != k + 1:
        raise ValueError("Length of alpha array should equal one plus\
                         lengths of thetas array.")

    n_levels = thetas.shape[1]

    if n_levels > n_dim:
        raise ValueError("Check theta subarray lengths.")

    # same as in displacement function definition
    upper_diag = list(map(lambda x: np.sqrt(x), range(1, n_dim)))

    a = sparse.diags(upper_diag, offsets=1).toarray()
    adag = a.conj().T

    L, P = scipy.linalg.eig(adag - a)

    Pp = P.conj().T

    # two initialised matrices
    matrix = np.eye(n_dim)
    temp = np.eye(n_dim)

    # construct ansatz step by step
    for i in range(0, k):
        # displacement gate
        matrix = Pp @ temp
        temp = np.diag(np.exp(alphas[i] * L)) @ matrix
        matrix = P @ temp

        # snap gate
        temp = sparse.diags(np.exp(
            1j*np.concatenate((thetas[i], np.zeros(n_dim - n_levels))
                              ))) @ matrix

    # final displacement gate
    matrix = Pp @ temp
    temp = np.diag(np.exp(alphas[k] * L)) @ matrix
    matrix = P @ temp

    # cost function using 'matrix' ansatz and U_target
    c = np.trace((U_target.conj().T) @ matrix) / n_dim
    cost = 1 - np.linalg.norm(c)

    return cost


def cost_state(psi, alphas, thetas, n_dim):
    """
    cost_state function used in optimiser, and as general figure of merit.
    This cost function uses a target state.

    Parameters
    ----------
    psi : 1D numpy array of complex floats
        the ket (not bra) form of a targe state of the cavity
    alphas : 1D numpy array of floats
        array of displacement parameters (the first element of array
        corresponds with the first (in time, i.e. rightmost matrix)
        displacement gate, etc.)
    thetas : 2D numpy array of floats
        array of angles for SNAP gates. The outermost index i gives an
        array corresponding to the angles applied in the i+1'th SNAP gate
        (0 <= i < thetas.shape[0]). thetas[i] (with length <= n_dim)
        corresponds with the first SNAP gate above, thetas[1], the second
        SNAP gate, etc. All thetas[i] arrays must have the same length.
    n_dim : int
        size of matrix representation of realised state

    Returns
    -------
    float
        the cost function evaluated between the target cavity state,
        and the realised state that is reconstructed from the ansatz
        unitary, with the alphas and thetas input parameters, acting
        on a cavity initially in the ground state
    """

    # check target state size is compatible
    if psi.shape != (n_dim,):
        raise Exception("Target state size incompatible.")

    # check that psi is normalised
    assert np.isclose(psi.conj() @ psi, 1), "State psi is not normalised."

    k = thetas.shape[0]

    if len(alphas) != k + 1:
        raise ValueError("Length of alpha array should equal one plus\
                         lengths of thetas array.")

    n_levels = thetas.shape[1]

    if n_levels > n_dim:
        raise ValueError("Check theta subarray lengths.")

    # same as in displacement function definition
    upper_diag = list(map(lambda x: np.sqrt(x), range(1, n_dim)))

    a = sparse.diags(upper_diag, offsets=1).toarray()
    adag = a.conj().T

    L, P = scipy.linalg.eig(adag - a)

    Pp = P.conj().T

    # two initialised states
    state = np.zeros(n_dim)
    state[0] = 1

    temp = np.zeros(n_dim)
    temp[0] = 1

    # construct ansatz step by step
    for i in range(0, k):
        # displacement gate
        state = Pp @ temp
        temp = np.diag(np.exp(alphas[i] * L)) @ state
        state = P @ temp

        # snap gate
        temp = sparse.diags(np.exp(
            1j*np.concatenate((thetas[i], np.zeros(n_dim - n_levels))
                              ))) @ state

    # final displacement gate
    state = Pp @ temp
    temp = np.diag(np.exp(alphas[k] * L)) @ state
    state = P @ temp

    # overlap between state ansatz and psi
    c = (psi.conj()) @ state
    cost = 1 - np.linalg.norm(c)

    return cost


def gradient_cost_unitary(U_target, alphas, thetas, n_dim):
    """
    gradient_cost_unitary gives the derivative of the cost_unitary
    function with respect to each of the parameters in alphas and
    thetas arrays

    Parameters
    ----------
    same as cost_unitary function

    Returns
    -------
    2-tuple of 1D numpy array of floats
        the first element of the returned tuple is a k+1 (k = len(alphas))
        length numpy array corresponding to the derivative of the cost_unitary
        function with respect to each of the parameters in alphas vector,
        and the second element is a length k*n_levels 1D array (where
        n_levels = thetas.shape[1]), with the first n_levels elements
        corresponding to the derivative of the cost_unitary function with
        respect to the parameters of the first SNAP gate, the second
        n_levels elements corresponding to the derivative of the cost_unitary
        function with respect to the parameters of the second SNAP gate, etc.
    """

    if U_target.shape != (n_dim, n_dim):
        raise Exception("U_target size incompatible.")

    k = thetas.shape[0]

    if len(alphas) != k + 1:
        raise ValueError("Length of alpha array should equal one plus\
                         lengths of thetas array.")

    n_levels = thetas.shape[1]

    if n_levels > n_dim:
        raise ValueError("Check theta subarray lengths.")

    upper_diag = list(map(lambda x: np.sqrt(x), range(1, n_dim)))

    a = sparse.diags(upper_diag, offsets=1).toarray()
    adag = a.conj().T

    diff = adag - a

    L, P = scipy.linalg.eig(diff)

    Pp = P.conj().T

    matrix = np.eye(n_dim)
    temp = np.eye(n_dim)

    for i in range(0, k):
        matrix = Pp @ temp
        temp = np.diag(np.exp(alphas[i] * L)) @ matrix
        matrix = P @ temp

        temp = sparse.diags(np.exp(
            1j*np.concatenate((thetas[i],
                               np.zeros(n_dim - n_levels))))) @ matrix

    matrix = Pp @ temp
    temp = np.diag(np.exp(alphas[k] * L)) @ matrix
    matrix = P @ temp

    c = np.trace((U_target.conj().T) @ matrix) / n_dim

    # the rest of this function replaces expensive matrix exponentiation
    # with a few extra matrix multiplcations.

    A = U_target.conj().T.copy()
    B = matrix.copy()

    temp1 = np.eye(n_dim)
    temp2 = np.eye(n_dim)
    D = np.eye(n_dim)

    # lists to store gradient values
    grad_thetas_temp = []
    grad_alphas_temp = []

    # this confusing subroutine was transpiled my Julia code, as is, to
    # allow for any potential efficient BLAS routines in python
    # that I'm not aware of
    for i in range(0, k):
        temp1 = A @ B
        temp2 = temp1 @ diff

        grad_alphas_temp.append(np.trace(temp2))

        temp1 = B @ P
        B = temp1 @ np.diag(np.exp(-alphas[i] * L))
        temp1 = B @ Pp
        B = temp1 @ sparse.diags(np.exp(
            -1j*np.concatenate((thetas[i], np.zeros(n_dim - n_levels)))))

        temp1 = Pp @ A
        A = np.diag(np.exp(alphas[i] * L)) @ temp1
        temp1 = P @ A

        D = temp1 @ B

        for h in range(n_levels):
            grad_thetas_temp.append(1j * np.exp(1j * thetas[i, h]) * D[h, h])

        A = sparse.diags(np.exp(1j*np.concatenate(
            (thetas[i], np.zeros(n_dim - n_levels))))) @ temp1

    temp1 = A @ B
    temp2 = temp1 @ diff
    grad_alphas_temp.append(np.trace(temp2))

    # end of confusing subroutine #

    # change python lists to numpy arrays
    grad_alphas_temp = np.array(grad_alphas_temp)
    grad_thetas_temp = np.array(grad_thetas_temp)

    # check dimensions
    if len(grad_alphas_temp) != k+1:
        raise Exception("Error with alpha gradient array dimension.")

    if len(grad_thetas_temp) != n_levels*k:
        raise Exception("Error with theta gradient array dimension.")

    # gradients of cost function with respect to alpha parameters
    grad_alphas = (-1/(2 * n_dim * np.sqrt(np.conj(c) * c))) * \
        ((c * np.conj(grad_alphas_temp)) + (np.conj(c) * grad_alphas_temp))

    # gradient of cost function with respect to all theta parameters
    grad_thetas = (-1/(2 * n_dim * np.sqrt(np.conj(c) * c))) * \
        ((c * np.conj(grad_thetas_temp)) + (np.conj(c) * grad_thetas_temp))

    # above arrays are all complex floats with 0*1j imaginary parts, so
    # just coerce them into read arrays in the returned arrays

    return np.real(grad_alphas), np.real(grad_thetas)


def gradient_cost_state(psi, alphas, thetas, n_dim):
    """
    gradient_cost_state gives the derivative of the cost_state function
    with respect to each of the parameters in alphas and thetas arrays

    Parameters
    ----------
    same as cost_state function

    Returns
    -------
    2-tuple of 1D numpy array of floats
        the first element of the returned tuple is a k+1 (k = len(alphas))
        length numpy array corresponding to the derivative of the cost_state
        function with respect to each of the parameters in alphas vector,
        and the second element is a length k*n_levels 1D array (where
        n_levels = thetas.shape[1]), with the first n_levels elements
        corresponding to the derivative of the cost_state function with
        respect to the parameters of the first SNAP gate, the second
        n_levels elements corresponding to the derivative of the cost_state
        function with respect to the parameters of the second SNAP gate, etc.
    """

    if psi.shape != (n_dim,):
        raise Exception("Target state size incompatible.")

    assert np.isclose(psi.conj() @ psi, 1), "State psi is not normalised."

    k = thetas.shape[0]

    if len(alphas) != k + 1:
        raise ValueError("Length of alpha array should equal one plus\
                         lengths of thetas array.")

    n_levels = thetas.shape[1]

    if n_levels > n_dim:
        raise ValueError("Check theta subarray lengths.")

    # upper_diag = list(map(lambda x: np.sqrt(x), range(1, n_dim)))
    upper_diag = [np.sqrt(x) for x in range(1, n_dim)]

    a = sparse.diags(upper_diag, offsets=1).toarray()
    adag = a.conj().T

    diff = adag - a

    L, P = scipy.linalg.eig(diff)

    Pp = P.conj().T

    state = np.zeros(n_dim)
    state[0] = 1

    temp = np.zeros(n_dim)
    temp[0] = 1

    for i in range(0, k):
        # displacement gate
        state = Pp @ temp
        temp = np.diag(np.exp(alphas[i] * L)) @ state
        state = P @ temp

        # snap gate
        temp = sparse.diags(np.exp(1j*np.concatenate(
            (thetas[i], np.zeros(n_dim - n_levels))))) @ state

    # final displacement gate
    state = Pp @ temp
    temp = np.diag(np.exp(alphas[k] * L)) @ state
    state = P @ temp

    # overlap between state ansatz and state
    c = (psi.conj()) @ state

    # the rest of this function replaces expensive matrix exponentiation
    # with a few extra matrix multiplcations on states

    # copies of state and target state psi
    bra_state = psi.conj().copy()
    ket_state = state.copy()

    # lists to store gradient values
    grad_thetas_temp = []
    grad_alphas_temp = []

    # this subroutine allows for any potential efficient BLAS routines
    # in python that I'm not aware of

    grad_alphas_temp.append(bra_state @ diff @ ket_state)

    bra_state_2 = bra_state @ P
    bra_state = bra_state_2 @ np.diag(np.exp(alphas[k] * L))
    bra_state_2 = bra_state @ Pp

    ket_state_2 = Pp @ ket_state
    ket_state = np.diag(np.exp(-alphas[k] * L)) @ ket_state_2
    ket_state_2 = P @ ket_state

    for i in range(k - 1, -1, -1):

        ket_state = sparse.diags(np.exp(-1j*np.concatenate(
            (thetas[i], np.zeros(n_dim - n_levels))))) @ ket_state_2

        for h in range(n_levels - 1, -1, -1):

            grad_thetas_temp.append(1j * np.exp(1j * thetas[i, h]) *
                                    bra_state_2[h] * ket_state[h])

        bra_state = bra_state_2 @ sparse.diags(np.exp(
            1j*np.concatenate((thetas[i], np.zeros(n_dim - n_levels)))))

        grad_alphas_temp.append(bra_state @ diff @ ket_state)

        ket_state_2 = Pp @ ket_state
        ket_state = np.diag(np.exp(-alphas[i] * L)) @ ket_state_2
        ket_state_2 = P @ ket_state

        bra_state_2 = bra_state @ P
        bra_state = bra_state_2 @ np.diag(np.exp(alphas[i] * L))
        bra_state_2 = bra_state @ Pp

    # we iterated backwards, so flip the numpy arrays
    grad_alphas_temp = np.flip(grad_alphas_temp)
    grad_thetas_temp = np.flip(grad_thetas_temp)

    # end of subroutine #

    # check final dimensions
    if len(grad_alphas_temp) != k+1:
        raise Exception("Error with alpha gradient array dimension.")

    if len(grad_thetas_temp) != n_levels*k:
        raise Exception("Error with theta gradient array dimension.")

    # gradients of cost_state function with respect to alpha parameters
    grad_alphas = (-1/(2 * np.sqrt(np.conj(c) * c))) * \
        ((c * np.conj(grad_alphas_temp)) + (np.conj(c) * grad_alphas_temp))

    # gradient of cost_state function with respect to all theta parameters
    grad_thetas = (-1/(2 * np.sqrt(np.conj(c) * c))) * \
        ((c * np.conj(grad_thetas_temp)) + (np.conj(c) * grad_thetas_temp))

    # above arrays are all complex floats with 0*1j imaginary parts, so
    # just coerce them into read arrays in the returned arrays

    return np.real(grad_alphas), np.real(grad_thetas)


def gradient_cost_unitary_flat(U_target, alphas, thetas, n_dim):
    """
    gradient_cost_unitary_flat is a wrapper function that returns a
    concatenated array of the two arrays from the gradient_cost_unitary
    function.

    Parameters
    ----------
    same as gradient_cost_unitary function

    Returns
    -------
    1D numpy array of floats
        same output as gradient_cost_unitary, joined into one array.
        The first k+1 elements (k = len(alphas)) correspond to the
        gradients with respect to the alpha parameters, the next n_levels
        (where n_levels = thetas.shape[1]) elements the gradient with
        respect to the parameters of the first SNAP, the next n_levels
        elements the gradient with respect to the parameters of
        the second SNAP, etc., the final n_levels elements the gradient
        with respect to the final, k'th SNAP.
    """

    if U_target.shape != (n_dim, n_dim):
        raise Exception("U_target size incompatible.")

    k = thetas.shape[0]

    if len(alphas) != k + 1:
        raise ValueError("Length of alpha array should equal one plus\
                         lengths of thetas array.")

    n_levels = thetas.shape[1]

    if n_levels > n_dim:
        raise ValueError("Check theta subarray lengths.")

    output = gradient_cost_unitary(U_target, alphas, thetas, n_dim)
    result = np.concatenate((output[0], output[1]))

    return result


def gradient_cost_state_flat(psi, alphas, thetas, n_dim):
    """
    gradient_cost_state_flat is a wrapper function that returns a concatenated
    array of the two arrays from the gradient_cost_state function.

    Parameters
    ----------
    same as gradient_cost_state function

    Returns
    -------
    1D numpy array of floats
        same output as gradient_cost_state, joined into one array.
        The first k+1 elements (k = len(alphas)) correspond to the
        gradients with respect to the alpha parameters, the next n_levels
        (where n_levels = thetas.shape[1]) elements the gradient with
        respect to the parameters of the first SNAP, the next n_levels
        elements the gradient with respect to the parameters of
        the second SNAP, etc., the final n_levels elements the gradient
        with respect to the final, k'th SNAP.
    """

    if psi.shape != (n_dim,):
        raise Exception("Target state size incompatible.")

    assert np.isclose(psi.conj() @ psi, 1), "State psi is not normalised."

    k = thetas.shape[0]

    if len(alphas) != k + 1:
        raise ValueError("Length of alpha array should equal one plus\
                         lengths of thetas array.")

    n_levels = thetas.shape[1]

    if n_levels > n_dim:
        raise ValueError("Check theta subarray lengths.")

    output = gradient_cost_state(psi, alphas, thetas, n_dim)
    result = np.concatenate((output[0], output[1]))

    return result


def params_to_alphas(params, k, n_levels):
    """
    Given a 1D 'params' numpy array of floats of length k + 1 +
    (n_levels*k), where the first k+1 elements correspond with the alpha
    displacement gate parameters used to create the ansatz unitary in
    the ansatzU function, and the remaining n_levels*k parameters the
    SNAP theta parameters), this function returns the k+1 alpha parameters.

    Parameters
    ----------
    params : 1D numpy array of floats
        Length k + 1 + (n_levels *k) array of flattened alpha and theta
        parameters
    k : int
        number of layers in ansatzU
    n_levels : int
        number of levels targeted by each SNAP operation in the params
        vector

    Returns
    -------
    1D numpy array of floats
        alpha parameters (i.e. first k+1 parameters in params array)
    """

    # check length of params
    if len(params) != k + 1 + (n_levels*k):
        raise ValueError("Length of params array does not match the\
                         length specified by the input k and n_levels\
                         function parameters.")

    return params[0:k+1]


def params_to_thetas(params, k, n_levels):
    """
    Given a 1D 'params' numpy array of floats of length k + 1 + (n_levels*k),
    where the first k+1 elements correspond with the alpha displacement
    gate parameters used to create the ansatz unitary in the ansatzU
    function, and the remaining n_levels*k parameters the SNAP theta
    parameters), this function returns a 2D, k by n_levels array of
    theta parameters, where the first row corresponds with the n_levels
    theta parameters in the first SNAP gate, the second row the theta
    parameters in the second SNAP gate, etc., the last, k'th row the
    n_levels theta parameters in the last, k'th SNAP gate.

    Parameters
    ----------
    params : 1D numpy array of floats
        Length k + 1 + (n_levels *k) array of flattened alpha and theta
        parameters
    k : int
        number of layers
    n_levels : int
        number of levels targeted by each SNAP operation in the params
        vector

    Returns
    -------
    2D, k by n_levels numpy array of floats
        theta parameters
    """

    # check length of params array
    if len(params) != k + 1 + (n_levels*k):
        raise ValueError("Length of params array does not match the\
                         length specified by the input k and n_levels\
                         function parameters.")

    return params[k+1:].reshape((k, n_levels))


def alphas_and_thetas_to_params(alphas, thetas, k, n_levels):
    """
    Essentially does the inverse of both params_to_thetas and
    params_to_alphas. When given a 1D array of alpha parameters
    and a 2D array of theta parameters, this returns a 1D parameter
    array.

    Note that feeding the output of params_to_alphas and
    params_to_thetas (for a fixed params array) into this function,
    should identically return said params array

    Parameters
    ----------
    alphas : 1D numpy array
        alpha parameters, should be of length k+1
    thetas : 2D numpy array
        theta parameters, should have shape (k, n_levels)
    k : int
        number of layers
    n_levels : int
        number of Fock levels of cavity mode that are targeted by each
        SNAP gate

    Returns
    -------
    1D numpy array of floats
        Length k + 1 + (k*n_levels) parameter array containing alphas
        and thetas
    """

    # check shapes
    if alphas.shape[0] != k+1:
        raise Exception("Error with either k input parameter or\
                        alphas array.")

    if thetas.shape != (k, n_levels):
        raise Exception("Error with either k or n_levels input\
                        parameters, or theta 2D parameter array")

    # flatten thetas array
    theta_array = thetas.reshape(k*n_levels)

    return np.concatenate((alphas, theta_array))


def cost_unitaries(A_unitary, B_unitary):
    """
    cost_unitaries returns the cost function between two unitary matrices
    A and B (according to the above 'cost_unitary' function definition)

    Parameters
    ----------
    A_unitary : 2D numpy array
        unitary matrix A
    B_unitary : 2D numpy array
        unitary matrix B

    Returns
    -------
    float
        cost between two matrices
    """

    # check that both matrices are indeed unitary
    assert np.allclose(np.eye(A_unitary.shape[0]), A_unitary @
                       (A_unitary.conj().T)), "Unitary A matrix not really \
                        unitary"

    assert np.allclose(np.eye(B_unitary.shape[0]), B_unitary @
                       (B_unitary.conj().T)), "Unitary B matrix not really \
                        unitary"

    return 1 - np.linalg.norm(np.trace((A_unitary.conj().T) @ B_unitary)
                              / A_unitary.shape[0])


def cost_states(A_state, B_state):
    """
    cost_states returns the cost function between two states
    A and B (according to the above 'cost_state' function definition)

    Parameters
    ----------
    A_unitary : 1D numpy array
        state A
    B_unitary : 1D numpy array
        state B

    Returns
    -------
    float
        cost between two states
    """

    # check that A_state is normalised
    assert np.isclose(A_state.conj() @ A_state, 1),\
        "State A is not normalised."
    # check that B_state is normalised
    assert np.isclose(B_state.conj() @ B_state, 1),\
        "State B is not normalised."

    return 1 - np.linalg.norm(A_state.conj() @ B_state)


def pulse_parameter_finder(target, k, n_dim, n_levels=None, max_runs=50, err_th = 0.01, d_fid = None,
                           initial_guess = None, method='L-BFGS-B', realised_check=False):
    """
    pulse_parameter_finder, the main result for this section, for a
    given target (either a target unitary or target state), returns the
    alpha and theta parameters appearing in the ansatzU (for k layers
    with each SNAP in each layer acting on the first n_levels Fock
    states) such that the ansatz unitary best approximates either
    the input target unitary, or best gives the target state (assuming
    that the ansatz unitary acts on a cavity initialised in the ground
    state).

    Parameters
    ----------
    target : 1D/2D numpy array of complex floats
        input target state/unitary to find parameter sequence for
    k : int
        number of different displacement+snap layers
    n_dim : int
        size of matrix representations employed in calculations
    n_levels : int, optional
        the number of Fock states each SNAP gate in each layer acts on.
        When no n_levels parameter is given, n_levels defaults to n_dim.
    runs : int, optional
        the number of independent optimiser searches, with different
        sets of starting parameters, carried out by the optimiser, with
        the best solution being kept. If runs is set to 1, then the
        initial optimiser starting points, for both the alphas and thetas,
        are all 0. If runs > 1, then, in addition to 0's as a starting
        point, runs-1 random starting points (where the alpha/theta
        parameters are normally distributed/uniformly distributed) are
        also checked, by default 5
    method : str, optional
        optimisation algorithm used (see
        https://docs.scipy.org/doc/scipy/reference/generated/scipy.optimize.minimize.html),
        by default 'L-BFGS-B'
    realised_check : bool, optional
        if target is a unitary, check whether the final, realised
        unitary arising from the optimised parameters, is equal to the
        target unitary up to an arbitrary phase, by default False

    Returns
    -------
    4-tuple (scipy.optimize.minimize, 1D numpy array of floats, 1D/2D numpy
        array of complex floats, float) a 4-tuple containing the
        scipy.optimize.minimise object, the 1D numpy array of optimised
        alpha and theta parameters, a 1D/2D numpy array representation
        of the state/unitary resulting from the optimised parameters,
        and the final value of the cost function for the optimised parameters
    """

    if n_levels is None:
        n_levels = n_dim

    if target.ndim == 1:
        if target.shape != (n_dim,):
            raise Exception("Target state size incompatible.")

        assert np.isclose(target.conj() @ target, 1), \
            "Target state is not normalised."

    elif target.ndim == 2:
        if target.shape != (n_dim, n_dim):
            raise Exception("target size incompatible.")

        assert np.allclose(np.eye(n_dim), target @ (target.conj().T)),\
            "target doesn't appear to be unitary"

    if n_levels > n_dim:
        raise ValueError("Cannot target more Fock states than exist for\
                         matrix representation.")

    # wrapper function 'gradients' (to use in scipy.optimize.minimize)
    def gradient(x):
        alphas = params_to_alphas(x, k, n_levels)
        thetas = params_to_thetas(x, k, n_levels)

        if target.ndim == 1:
            return gradient_cost_state_flat(target, alphas, thetas, n_dim)
        elif target.ndim == 2:
            return gradient_cost_unitary_flat(target, alphas, thetas, n_dim)

    # wrapper function 'func' (to use in scipy.optimize.minimize)
    def func(x):
        alphas = params_to_alphas(x, k, n_levels)
        thetas = params_to_thetas(x, k, n_levels)
        # thetas = (np.round(thetas/np.pi))*np.pi # [TR]

        if target.ndim == 1:
            return cost_state(target, alphas, thetas, n_dim)
        elif target.ndim == 2:
            # U_realised = ansatzU(alphas, thetas, n_dim) # [TR]
            # return 1 - unitary_fidelity(target, U_realised)
            return cost_unitary(target, alphas, thetas, n_dim, d_fid) # [TR]
            # return cost_unitary(target, alphas, thetas, n_dim)


    # infinite starting error
    err = np.inf
    final_result = None

    # each run iteration runs the optimiser, and overall, the best
    # solution, with respect to the 'cost_states'/'cost_unitaries'
    # function, is kept
    for j in range(max_runs):

        # TODO: use SO2 and SNAP decomposition, and Josh's SO2 angle
        # relation, to find a better starting point?

        if j%10==0: print('Current itr#:',j)
        
        # if j = 0, start at zeros
        if j == 0:
            if initial_guess is None:
                initial_guess = np.concatenate((np.random.normal(0, np.sqrt(n_levels), k+1),
                                            (np.pi) * # Changed from 2pi to pi [TR]
                                            np.random.rand(k*n_levels)))
        else:
        
            # NOTE: no particular reason a variance of 5 was chosen here! -> sqrt(n_levels) [TR]
            initial_guess = (initial_guess + np.concatenate((np.random.normal(0, np.sqrt(n_levels), k+1),
                                            (np.pi) * # Changed from 2pi to pi [TR]
                                            np.random.rand(k*n_levels))) )/2

        # run optimiser - note that the cost_operators result may
        # still be quite large if the number of displacement+SNAP
        # layers is not large enough, and the optimiser may not converge.
        # We check whether this is the case outside of the enclosing
        # for loop iterator
        # TODO: choose different max number of iterations?
        result = minimize(func, initial_guess,
                                         method=method,
                                         jac=gradient,
                                         tol=10**(-5),
                                         options={"maxiter": 10000,
                                                  "disp": False})

        # Parameters which minimised the cost (func) function
        minimum = result.x
        initial_guess = minimum # [TR]

        # The realised unitary that is created using the optimised
        # displacement and SNAP parameters
        U_realised = ansatzU(params_to_alphas(minimum, k, n_levels),
                             params_to_thetas(minimum, k, n_levels),
                             n_dim)

        # compare cost between multiple runs, and save the best one
        if target.ndim == 1:
            cavity_state = U_realised[:, 0]

            cost = cost_states(target, cavity_state)
        
        elif target.ndim == 2:
            # cost = cost_unitaries(U_realised, target) # [TR]
            cost = cost_unitary(target, params_to_alphas(minimum, k, n_levels),
                                params_to_thetas(minimum, k, n_levels), n_dim, d_fid) # [TR]
            # cost = 1 - unitary_fidelity(target, U_realised) 
        
        if cost < err:
            err = cost
            final_result = result
            
        if cost <= err_th: break
        elif j==max_runs-1:
            final_result = result
            print("Warning: Error threshold could not be reached!")

    # Check whether optimiser worked
    if final_result is None:
        raise ValueError("final_result should not be None.")
    if not result.success:
        raise Exception("Optimiser did not converge successfully.")

    optimised_params = final_result.x

    U_realised = ansatzU(params_to_alphas(optimised_params, k, n_levels),
                         params_to_thetas(optimised_params, k, n_levels),
                         n_dim)

    # check whether U_target and U_realised are the same, up to an
    # arbitrary phase, by removing the top left element's phase
    # in U_target, and adding that of U_realised -> should
    # then give identical matrices
    if realised_check and (target.ndim == 1):

        cavity_state = U_realised[:, 0]

        target_set_phase = np.exp(-1j*np.angle(target[0])) * \
            np.exp(1j*np.angle(cavity_state[0])) * target

        assert np.allclose(target_set_phase, cavity_state, atol=1e-04), \
            "Realised state is not equal to the target state\
                within the accepted tolerance."

    elif realised_check and (target.ndim == 2):

        target_set_phase = np.exp(-1j*np.angle(target[0][0])) * \
                           np.exp(1j*np.angle(U_realised[0][0])) * target

        assert np.allclose(target_set_phase, U_realised, atol=1e-04), \
            "Realised unitary matrix is not equal to the target unitary\
                within the accepted tolerance."

    if target.ndim == 1:
        returned_object = U_realised[:, 0]
        fid = 1 - result.fun
    elif target.ndim == 2:
        returned_object = U_realised
        # fid = unitary_fidelity(target, U_realised) 
        # Above fidelity is calculated for total cut_off dimension d_cut

    # return final_result, optimised_params, returned_object, fid
    return final_result, optimised_params, returned_object, err


def display_params(params_1d, n_snap):
    ''' Prints optimized S+D parameters '''
    disp_arr = params_1d[:n_snap+1]
    snap_arr = params_1d[n_snap+1:]*180/np.pi
    d_snap = int(len(snap_arr)/n_snap)
    snap_arr = snap_arr.reshape(n_snap, d_snap)
    print("Displacements:", disp_arr)
    print("SNAP degrees:")
    print(np.round(snap_arr,1))

### == Changes made by TR == ###
#1 Added function to compute unitary fidelity (haven't verified for a vector)
#2 Modified the initial_guess: now it uses the last optimized values + some randomness
#3 Functionality that code stops if cost <= error threshold