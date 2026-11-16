# Rust Station

This project is aiming to be a station control & monitoring for the RC car.

# Concepts

## SLAM

### Grid

We need a grid for the HMI, and to plan trajectories.

#### Bresenham

The map is a fixed-size grid of square cells of a given size. Each cell stores a *log-odds* value.

For each scan point, we walk the cells from the sensor origin to the hit, using only integer arithmetic. The cells crossed are updated as free, and the hit cell as occupied.

**Notation**

$$dx = |x_1 - x_0|, \quad dy = |y_1 - y_0|, \quad s_x = \operatorname{sign}(x_1 - x_0), \quad s_y = \operatorname{sign}(y_1 - y_0)$$

$(i, j)$ is the current cell, as an offset from the start cell $(x_0, y_0)$.

**Line equation**

The line from the origin to $(dx, dy)$ is $dx \cdot y = dy \cdot x$. A cell $(i, j)$ is on the line if:

$$g(i, j) = dx \cdot j - dy \cdot i = 0$$

$g > 0$ means the cell is above the line, $g < 0$ below it. We write $r = g(i, j)$ for the current cell.

**Neighbours**

Since $g$ is linear, each step adds a fixed term:

$$g(i+1, j) = r - dy \qquad g(i, j+1) = r + dx \qquad g(i+1, j+1) = r + dx - dy$$

We pick the neighbour closest to the line, i.e. the smallest $|g|$.

**Definition of $err$**

$$err = g(i+1, j+1) = r + dx - dy$$

At the start $r = 0$, so $err = dx - dy$. The two other neighbours follow:

$$g(i+1, j) = err - dx \qquad g(i, j+1) = err + dy$$

**Decision**

Both sides of $|a| < |b|$ are non-negative, so it is equivalent to $a^2 < b^2$. For the first test:

$$|err| < |err - dx| \iff err^2 < err^2 - 2\,err\,dx + dx^2 \iff 2\,err < dx$$

and likewise for the second:

$$|err| < |err + dy| \iff 2\,err > -dy$$

The diagonal cell is closer to the line than:

- the x-only neighbour if $2\,err < dx$: the step must also move in $y$
- the y-only neighbour if $2\,err > -dy$: the step must also move in $x$

If both hold, the step is diagonal. Updates:

$$\text{step in } x: \; err \mathrel{-}= dy, \quad x \mathrel{+}= s_x$$
$$\text{step in } y: \; err \mathrel{+}= dx, \quad y \mathrel{+}= s_y$$

The loop stops when $(x, y) = (x_1, y_1)$. Each visited point are updated with adding a *log-odd* of $-0.4$, while the last point (hit point) is $+0.85$

**Log-Odds**

Cell occupancy is modeled using log-odds notation:

$$l = \ln\left(\frac{p}{1 - p}\right) \iff p = \frac{1}{1 + e^{-l}}$$

Using log-odds transforms the multiplicative Bayesian update into a computationally efficient addition:

$$l_{\text{new}} = l_{\text{old}} + \ln\left(\frac{p_{\text{measure}}}{1 - p_{\text{measure}}}\right)$$

For the core SLAM pipeline, we operate exclusively on the log-odds value $l$ to avoid expensive exponentiations ($e^{-l}$). Conversion back to standard probabilities $p \in [0, 1]$ is only performed when rendering or exporting the map (for instance grayscale visualization).

### 2×2 symmetric eigen-decomposition

For $A = \begin{pmatrix} a & b \\ b & d \end{pmatrix}$, with $\text{tr} = a + d$ and $\det = ad - b^2$:

$$\lambda_{\max/\min} = \frac{\text{tr}}{2} \pm \sqrt{\left(\frac{\text{tr}}{2}\right)^2 - \det}$$

The eigenvector of $\lambda$ is $v = (b,\; \lambda - a)^T$. If $b \approx 0$, $A$ is already diagonal and the eigenvectors are the axes.

### ICP

Point-to-line ICP. Reference: previous scans (KD-tree). Source: current scan.

**One iteration**

1. Project each current point $p$ into the world with the current pose estimate: $q = R(\theta)\,p + t$.
2. Search the 6 nearest neighbours of $q$ in the KD-tree (median split, alternating X/Y).
3. Filter the neighbours (too clustered, too far), then fit a line.

**Line fitting**

$$\mu = \frac{1}{N} \sum_{i=1}^{N} p_i \qquad \Sigma = \frac{1}{N} \sum_{i=1}^{N} (p_i - \mu)(p_i - \mu)^T$$

The wall direction $\text{dir}$ is the normalized eigenvector of $\lambda_{\max}$ (see above), and the normal is:

$$n = \begin{pmatrix} -\text{dir}_y \\ \text{dir}_x \end{pmatrix}$$

The orthogonal residual, with $c = \mu$ the centre of the line:

$$e = (q - c) \cdot n$$

**Gauss–Newton**

State $x = (t_x, t_y, \theta)^T$. For each point, the Jacobian is a row:

$$J = \frac{\partial e}{\partial x} = \begin{pmatrix} n_x & n_y & J_\theta \cdot n \end{pmatrix}, \qquad J_\theta = \begin{pmatrix} -(q_y - t_y) \\ q_x - t_x \end{pmatrix}$$

Accumulated over all matched points:

$$H = \sum J^T J, \qquad g = \sum J^T e, \qquad H\,\Delta x = -g$$

Apply $\Delta x$, then repeat until the step is below the thresholds. Two stages: a coarse one (large gating distance), then a fine one (tight gating distance).

### NDT

Each cell stores the mean, covariance and information of the previous scans' points that fall in it:

$$\mu = \frac{1}{N} \sum_{i=1}^{N} p_i \qquad \Sigma = \frac{1}{N} \sum_{i=1}^{N} (p_i - \mu)(p_i - \mu)^T \qquad \Lambda = \Sigma^{-1}$$

Points on a wall are aligned, so $\Sigma$ has rank 1. Before inverting, a cell needs at least $N_{\min}$ points and $\lambda_{\min}$ is clamped: $\lambda_{\min} \leftarrow \max(\lambda_{\min}, \varepsilon\,\lambda_{\max})$.

**Mahalanobis** distance of a point $p$ to a cell, with $\Delta = p - \mu$:

$$\text{Maha} = \Delta^T \Lambda\, \Delta$$

#### Align

Unlike the original NDT (Biber & Straßer), which maximizes $\sum \exp(-\tfrac12 q^T \Lambda q)$, we minimize a least-squares cost with Mahalanobis gating.

**One iteration**

1. Project each scan point from the car frame to the world with the current pose: $p_w = R(\theta)\,p + t$.
2. Among the 3×3 cells around $p_w$, keep those with $\text{Maha} \le \gamma$, and choose the one whose mean $\mu_i$ is the closest to $p_w$ (Euclidean).
3. Accumulate the cost, gradient and Hessian, add the regularization, solve, clamp, apply.

**Cost function**

$$E(x) = E_{\text{NDT}}(x) + E_{\text{reg}}(x), \qquad E_{\text{NDT}}(x) = \sum_i \tfrac12\, q_i^T \Lambda_i\, q_i, \qquad q_i = p_{w,i} - \mu_i$$

**Gauss–Newton**

With $x = (t_x, t_y, \theta)^T$ and $J_\theta = \left(-(y_w - t_y),\; x_w - t_x\right)^T$, the Jacobian is $J = \partial p_w / \partial x = \begin{pmatrix} I_2 & J_\theta \end{pmatrix}$. Per point:

$$g_i = \begin{pmatrix} \Lambda q \\ J_\theta^T \Lambda q \end{pmatrix} \qquad H_i = \begin{pmatrix} \Lambda & \Lambda J_\theta \\ J_\theta^T \Lambda & J_\theta^T \Lambda J_\theta \end{pmatrix}$$

and $g = \sum g_i$, $H = \sum H_i$. The step solves $(H + H_{\text{reg}})\,\Delta x = -(g + g_{\text{reg}})$.

**Weakest direction (diagnostic)**

Take the top-left $2\times2$ block of $H$, **before** adding the regularization. Its eigenvalues give the strongest and weakest information. The weakest direction is:

$$\alpha = \text{atan2}(\lambda_{\min} - a,\; b), \qquad w = (\cos\alpha,\; \sin\alpha)^T$$

**Regularization**

In a corridor, no point constrains the motion along its axis: $H$ is singular in the direction $w$ and the inversion fails. We add a prior that pulls the solution towards the initial pose $x_0$ (odometry prediction). With $\delta = x - x_0 = (\delta_t,\, \delta_\theta)$:

$$E_{\text{reg}} = \tfrac12\, w_t\, (w^T \delta_t)^2 + \tfrac12\, w_t\, \delta_\theta^2$$

$$g_{\text{reg}} = \begin{pmatrix} w_t\,(w^T \delta_t)\, w \\ w_t\,\delta_\theta \end{pmatrix} \qquad H_{\text{reg}} = \begin{pmatrix} w_t\, w w^T & 0 \\ 0 & w_t \end{pmatrix}$$

**Step limiting and stop**

The step is scaled, not clamped component by component, so its direction is preserved:

$$k = \min\left(1,\; \frac{0.10\ \text{m}}{\|\Delta t\|},\; \frac{3^\circ}{|\Delta\theta|}\right), \qquad x \leftarrow x + k\,\Delta x$$

Stop when $\|\Delta t\| < \varepsilon_t$ and $|\Delta\theta| < \varepsilon_\theta$, or after 14 iterations.